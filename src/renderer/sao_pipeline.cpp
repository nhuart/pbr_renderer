#include "renderer/sao_pipeline.hpp"
#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "core/swapchain.hpp"
#include "scene/types.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_inverse.hpp>


static vk::raii::Sampler makeNearestSampler(VulkanContext const& ctx) {
    return vk::raii::Sampler(ctx.device, vk::SamplerCreateInfo{
        .magFilter = vk::Filter::eNearest,
        .minFilter = vk::Filter::eNearest,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .minLod = 0.0f,
        .maxLod = 0.0f,
    });
}

static std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> makeUboBuffer(
        VulkanContext const& ctx, vk::DeviceSize size) {
    return vkutil::createBuffer(ctx, size, vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
}


SaoPipeline::SaoPipeline(VulkanContext const& ctx, Swapchain const& swapchain,
        SaoConfig const& cfg)
        : config(cfg) {
    createImages(ctx, swapchain.extent);
    createNormalsPass(ctx, swapchain);
    createSaoPass(ctx);
    createBlurPass(ctx);
}


void SaoPipeline::createImages(VulkanContext const& ctx, vk::Extent2D extent) {
    constexpr vk::Format normalsFormat = vk::Format::eR8G8B8A8Unorm;
    // R=AO value (0=occluded, 1=unoccluded); G+B=linearized depth packed as 16-bit (G*256/257 + B/257)
    // for bilateral blur: depth is compared per-neighbor to avoid blurring AO across depth discontinuities
    constexpr vk::Format aoFormat      = vk::Format::eR8G8B8A8Unorm;
    vk::Format depthFormat = vkutil::findDepthFormat(ctx);

    auto createImage = [&](vk::Format format, vk::ImageUsageFlags usage)
            -> std::pair<vk::raii::Image, vk::raii::DeviceMemory> {
        return vkutil::createImage(ctx, extent.width, extent.height, 1,
                vk::SampleCountFlagBits::e1, format, vk::ImageTiling::eOptimal, usage,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
    };

    constexpr vk::ImageUsageFlags colorUsage =
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
            vk::ImageUsageFlagBits::eTransferSrc;

    // Single-sample depth (used for normals prepass depth write + SAO depth read)
    auto [di, dm] = createImage(depthFormat,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled);
    depthImage  = std::move(di);
    depthMemory = std::move(dm);
    depthView   = vkutil::createImageView(ctx, *depthImage, depthFormat,
            vk::ImageAspectFlagBits::eDepth);
    depthSampler = makeNearestSampler(ctx);

    auto [ni, nm] = createImage(normalsFormat, colorUsage);
    normalsImage  = std::move(ni);
    normalsMemory = std::move(nm);
    normalsView   = vkutil::createImageView(ctx, *normalsImage, normalsFormat);
    normalsSampler = makeNearestSampler(ctx);

    auto [ri, rm] = createImage(aoFormat, colorUsage);
    aoRawImage  = std::move(ri);
    aoRawMemory = std::move(rm);
    aoRawView   = vkutil::createImageView(ctx, *aoRawImage, aoFormat);
    aoRawSampler = makeNearestSampler(ctx);

    auto [bi, bm] = createImage(aoFormat, colorUsage);
    aoBlurImage  = std::move(bi);
    aoBlurMemory = std::move(bm);
    aoBlurView   = vkutil::createImageView(ctx, *aoBlurImage, aoFormat);
    aoBlurSampler = makeNearestSampler(ctx);
}


void SaoPipeline::createNormalsPass(VulkanContext const& ctx, Swapchain const& /*swapchain*/) {
    // Descriptor set layout: binding 0 = UniformBufferObject (vert+frag)
    std::array<vk::DescriptorSetLayoutBinding, 1> bindings{ {
        { .binding = 0,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eVertex |
                          vk::ShaderStageFlagBits::eFragment },
    } };
    normalsDescLayout = vk::raii::DescriptorSetLayout(ctx.device,
            vk::DescriptorSetLayoutCreateInfo{
                .bindingCount = static_cast<uint32_t>(bindings.size()),
                .pBindings = bindings.data(),
            });

    // Pool is allocated lazily by allocateNormalsObjects; create a minimal placeholder.
    // The pool will be recreated when allocateNormalsObjects is called.
    std::array<vk::DescriptorPoolSize, 1> poolSizes{ {
        { .type = vk::DescriptorType::eUniformBuffer, .descriptorCount = 1 },
    } };
    normalsDescPool = vk::raii::DescriptorPool(ctx.device,
            vk::DescriptorPoolCreateInfo{
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = 1,
                .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                .pPoolSizes = poolSizes.data(),
            });

    vk::DescriptorSetLayout dslHandle = *normalsDescLayout;
    normalsPipeLayout = vk::raii::PipelineLayout(ctx.device, vk::PipelineLayoutCreateInfo{
                                                                 .setLayoutCount = 1,
                                                                 .pSetLayouts = &dslHandle,
                                                             });

    auto vertCode = vkutil::readSpirv("shaders/compiled/standard.vert.spv");
    auto fragCode = vkutil::readSpirv("shaders/compiled/view_space_normals.frag.spv");
    auto vertMod = vkutil::createShaderModule(ctx, vertCode);
    auto fragMod = vkutil::createShaderModule(ctx, fragCode);

    std::array stages = {
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = *vertMod,
            .pName = "main",
        },
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = *fragMod,
            .pName = "main",
        },
    };

    std::vector<vk::DynamicState> dynStates = { vk::DynamicState::eViewport,
        vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynInfo{
        .dynamicStateCount = static_cast<uint32_t>(dynStates.size()),
        .pDynamicStates = dynStates.data(),
    };

    auto binding = Vertex::getBindingDescription();
    auto attribs = Vertex::getAttributeDescriptions();
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size()),
        .pVertexAttributeDescriptions = attribs.data(),
    };

    vk::PipelineInputAssemblyStateCreateInfo iaInfo{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };
    vk::PipelineViewportStateCreateInfo vpInfo{ .viewportCount = 1, .scissorCount = 1 };
    vk::PipelineRasterizationStateCreateInfo rsInfo{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .lineWidth = 1.0f,
    };
    vk::PipelineMultisampleStateCreateInfo msInfo{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
    };
    vk::PipelineColorBlendAttachmentState cbAttach{
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    vk::PipelineColorBlendStateCreateInfo cbInfo{
        .attachmentCount = 1,
        .pAttachments = &cbAttach,
    };
    vk::PipelineDepthStencilStateCreateInfo dsInfo{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
        .depthCompareOp = vk::CompareOp::eLess,
    };

    constexpr vk::Format normalsFormat = vk::Format::eR8G8B8A8Unorm;
    vk::Format depthFormat = vkutil::findDepthFormat(ctx);

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
        {
            .stageCount = static_cast<uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &iaInfo,
            .pViewportState = &vpInfo,
            .pRasterizationState = &rsInfo,
            .pMultisampleState = &msInfo,
            .pDepthStencilState = &dsInfo,
            .pColorBlendState = &cbInfo,
            .pDynamicState = &dynInfo,
            .layout = *normalsPipeLayout,
        },
        {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &normalsFormat,
            .depthAttachmentFormat = depthFormat,
        },
    };
    normalsPipeline = vk::raii::Pipeline(ctx.device, nullptr,
            chain.get<vk::GraphicsPipelineCreateInfo>());
    std::cout << "SAO normals prepass pipeline: created\n";
}

void SaoPipeline::createSaoPass(VulkanContext const& ctx) {
    constexpr uint32_t frameCount = MAX_FRAMES_IN_FLIGHT;

    // bindings: 0=SaoUBO, 1=depthSampler, 2=normalSampler
    std::array<vk::DescriptorSetLayoutBinding, 3> bindings{ {
        { 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment },
        { 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
        { 2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
    } };
    saoDescLayout = vk::raii::DescriptorSetLayout(ctx.device,
            vk::DescriptorSetLayoutCreateInfo{
                .bindingCount = static_cast<uint32_t>(bindings.size()),
                .pBindings = bindings.data(),
            });

    std::array<vk::DescriptorPoolSize, 2> poolSizes{ {
        { vk::DescriptorType::eUniformBuffer, frameCount },
        { vk::DescriptorType::eCombinedImageSampler, frameCount * 2 },
    } };
    saoDescPool = vk::raii::DescriptorPool(ctx.device,
            vk::DescriptorPoolCreateInfo{
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = frameCount,
                .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                .pPoolSizes = poolSizes.data(),
            });

    for (uint32_t i = 0; i < frameCount; i++) {
        auto [buf, mem] = makeUboBuffer(ctx, sizeof(SaoUBO));
        saoUboMapped.push_back(mem.mapMemory(0, sizeof(SaoUBO)));
        saoUboBuffers.push_back(std::move(buf));
        saoUboMemory.push_back(std::move(mem));
    }

    std::vector<vk::DescriptorSetLayout> layouts(frameCount, *saoDescLayout);
    auto sets = vk::raii::DescriptorSets(ctx.device, vk::DescriptorSetAllocateInfo{
                                                         .descriptorPool = *saoDescPool,
                                                         .descriptorSetCount = frameCount,
                                                         .pSetLayouts = layouts.data(),
                                                     });

    vk::DescriptorImageInfo normalsInfo{
        .sampler = *normalsSampler,
        .imageView = *normalsView,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    vk::DescriptorImageInfo depthInfo{
        .sampler = *depthSampler,
        .imageView = *depthView,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };

    for (uint32_t i = 0; i < frameCount; i++) {
        vk::DescriptorBufferInfo uboInfo{ *saoUboBuffers[i], 0, sizeof(SaoUBO) };
        std::array<vk::WriteDescriptorSet, 3> writes{ {
            { .dstSet = *sets[i], .dstBinding = 0, .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer, .pBufferInfo = &uboInfo },
            { .dstSet = *sets[i], .dstBinding = 1, .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &depthInfo },
            { .dstSet = *sets[i], .dstBinding = 2, .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &normalsInfo },
        } };
        ctx.device.updateDescriptorSets(writes, {});
        saoDescSets.push_back(std::move(sets[i]));
    }

    vk::DescriptorSetLayout dslHandle = *saoDescLayout;
    saoPipeLayout = vk::raii::PipelineLayout(ctx.device, vk::PipelineLayoutCreateInfo{
                                                             .setLayoutCount = 1,
                                                             .pSetLayouts = &dslHandle,
                                                         });

    auto vertCode = vkutil::readSpirv("shaders/compiled/fullscreen.vert.spv");
    auto fragCode = vkutil::readSpirv("shaders/compiled/sao.frag.spv");
    auto vertMod = vkutil::createShaderModule(ctx, vertCode);
    auto fragMod = vkutil::createShaderModule(ctx, fragCode);

    std::array stages = {
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = *vertMod,
            .pName = "main",
        },
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = *fragMod,
            .pName = "main",
        },
    };

    std::vector<vk::DynamicState> dynStates = { vk::DynamicState::eViewport,
        vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynInfo{
        .dynamicStateCount = static_cast<uint32_t>(dynStates.size()),
        .pDynamicStates = dynStates.data(),
    };
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vk::PipelineInputAssemblyStateCreateInfo iaInfo{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };
    vk::PipelineViewportStateCreateInfo vpInfo{ .viewportCount = 1, .scissorCount = 1 };
    vk::PipelineRasterizationStateCreateInfo rsInfo{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .lineWidth = 1.0f,
    };
    vk::PipelineMultisampleStateCreateInfo msInfo{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
    };
    constexpr vk::Format aoFormat = vk::Format::eR8G8B8A8Unorm;
    vk::PipelineColorBlendAttachmentState cbAttach{
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    vk::PipelineColorBlendStateCreateInfo cbInfo{
        .attachmentCount = 1, .pAttachments = &cbAttach,
    };

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
        {
            .stageCount = static_cast<uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &iaInfo,
            .pViewportState = &vpInfo,
            .pRasterizationState = &rsInfo,
            .pMultisampleState = &msInfo,
            .pColorBlendState = &cbInfo,
            .pDynamicState = &dynInfo,
            .layout = *saoPipeLayout,
        },
        {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &aoFormat,
        },
    };
    saoPipeline = vk::raii::Pipeline(ctx.device, nullptr,
            chain.get<vk::GraphicsPipelineCreateInfo>());
    std::cout << "SAO occlusion pipeline: created\n";
}


void SaoPipeline::createBlurPass(VulkanContext const& ctx) {
    constexpr uint32_t frameCount = MAX_FRAMES_IN_FLIGHT;
    constexpr uint32_t passesPerFrame = 2; // H + V

    // bindings: 0=BlurUBO, 1=aoSampler (depth packed in GB channels, no separate depth sampler)
    std::array<vk::DescriptorSetLayoutBinding, 2> bindings{ {
        { 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment },
        { 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
    } };
    blurDescLayout = vk::raii::DescriptorSetLayout(ctx.device,
            vk::DescriptorSetLayoutCreateInfo{
                .bindingCount = static_cast<uint32_t>(bindings.size()),
                .pBindings = bindings.data(),
            });

    uint32_t totalSets = frameCount * passesPerFrame;
    std::array<vk::DescriptorPoolSize, 2> poolSizes{ {
        { vk::DescriptorType::eUniformBuffer, totalSets },
        { vk::DescriptorType::eCombinedImageSampler, totalSets },
    } };
    blurDescPool = vk::raii::DescriptorPool(ctx.device,
            vk::DescriptorPoolCreateInfo{
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = totalSets,
                .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                .pPoolSizes = poolSizes.data(),
            });

    // Allocate UBOs and descriptor sets
    // Pass 0 (H): reads aoRaw, writes aoBlur
    // Pass 1 (V): reads aoBlur, writes aoRaw
    for (uint32_t fi = 0; fi < frameCount; fi++) {
        blurUboBuffers.push_back({ vk::raii::Buffer{ nullptr }, vk::raii::Buffer{ nullptr } });
        blurUboMemory.push_back(
                { vk::raii::DeviceMemory{ nullptr }, vk::raii::DeviceMemory{ nullptr } });
        blurUboMapped.push_back({ nullptr, nullptr });
        blurDescSets.push_back(
                { vk::raii::DescriptorSet{ nullptr }, vk::raii::DescriptorSet{ nullptr } });

        for (uint32_t pi = 0; pi < passesPerFrame; pi++) {
            auto [buf, mem] = makeUboBuffer(ctx, sizeof(BlurUBO));
            blurUboMapped[fi][pi] = mem.mapMemory(0, sizeof(BlurUBO));
            blurUboBuffers[fi][pi] = std::move(buf);
            blurUboMemory[fi][pi] = std::move(mem);

            BlurUBO blurUbo{
                .passIndex = static_cast<int>(pi),
                .farPlaneOverEdgeDistance = 0.0f, // updated each frame via updateUBOs
                .kernelRadius = config.kernelRadius,
            };
            memcpy(blurUboMapped[fi][pi], &blurUbo, sizeof(blurUbo));
        }

        // Allocate 2 descriptor sets for this frame
        std::vector<vk::DescriptorSetLayout> layouts(passesPerFrame, *blurDescLayout);
        auto sets = vk::raii::DescriptorSets(ctx.device,
                vk::DescriptorSetAllocateInfo{
                    .descriptorPool = *blurDescPool,
                    .descriptorSetCount = passesPerFrame,
                    .pSetLayouts = layouts.data(),
                });

        // Pass 0: reads aoRaw (binding 1 — depth packed in GB)
        // Pass 1: reads aoBlur (binding 1 — depth packed in GB)
        for (uint32_t pi = 0; pi < passesPerFrame; pi++) {
            vk::ImageView aoView = (pi == 0) ? *aoRawView : *aoBlurView;
            vk::Sampler   aoSamp = (pi == 0) ? *aoRawSampler : *aoBlurSampler;
            vk::DescriptorImageInfo aoInfo{
                .sampler = aoSamp,
                .imageView = aoView,
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            };

            vk::DescriptorBufferInfo uboInfo{ *blurUboBuffers[fi][pi], 0, sizeof(BlurUBO) };
            std::array<vk::WriteDescriptorSet, 2> writes{ {
                { .dstSet = *sets[pi], .dstBinding = 0, .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eUniformBuffer, .pBufferInfo = &uboInfo },
                { .dstSet = *sets[pi], .dstBinding = 1, .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                    .pImageInfo = &aoInfo },
            } };
            ctx.device.updateDescriptorSets(writes, {});
            blurDescSets[fi][pi] = std::move(sets[pi]);
        }
    }

    vk::DescriptorSetLayout dslHandle = *blurDescLayout;
    blurPipeLayout = vk::raii::PipelineLayout(ctx.device, vk::PipelineLayoutCreateInfo{
                                                              .setLayoutCount = 1,
                                                              .pSetLayouts = &dslHandle,
                                                          });

    auto vertCode = vkutil::readSpirv("shaders/compiled/fullscreen.vert.spv");
    auto fragCode = vkutil::readSpirv("shaders/compiled/sao_blur.frag.spv");
    auto vertMod = vkutil::createShaderModule(ctx, vertCode);
    auto fragMod = vkutil::createShaderModule(ctx, fragCode);

    std::array stages = {
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = *vertMod,
            .pName = "main",
        },
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = *fragMod,
            .pName = "main",
        },
    };

    std::vector<vk::DynamicState> dynStates = { vk::DynamicState::eViewport,
        vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynInfo{
        .dynamicStateCount = static_cast<uint32_t>(dynStates.size()),
        .pDynamicStates = dynStates.data(),
    };
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vk::PipelineInputAssemblyStateCreateInfo iaInfo{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };
    vk::PipelineViewportStateCreateInfo vpInfo{ .viewportCount = 1, .scissorCount = 1 };
    vk::PipelineRasterizationStateCreateInfo rsInfo{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .lineWidth = 1.0f,
    };
    vk::PipelineMultisampleStateCreateInfo msInfo{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
    };
    constexpr vk::Format aoFormat = vk::Format::eR8G8B8A8Unorm;
    vk::PipelineColorBlendAttachmentState cbAttach{
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    vk::PipelineColorBlendStateCreateInfo cbInfo{
        .attachmentCount = 1, .pAttachments = &cbAttach,
    };

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
        {
            .stageCount = static_cast<uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &iaInfo,
            .pViewportState = &vpInfo,
            .pRasterizationState = &rsInfo,
            .pMultisampleState = &msInfo,
            .pColorBlendState = &cbInfo,
            .pDynamicState = &dynInfo,
            .layout = *blurPipeLayout,
        },
        {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &aoFormat,
        },
    };
    blurPipeline = vk::raii::Pipeline(ctx.device, nullptr,
            chain.get<vk::GraphicsPipelineCreateInfo>());
    std::cout << "SAO blur pipeline: created\n";
}


void SaoPipeline::allocateNormalsObjects(VulkanContext const& ctx, uint32_t objectCount) {
    normalsObjects.clear();

    // Recreate pool sized for all objects
    uint32_t totalSets = objectCount * MAX_FRAMES_IN_FLIGHT;
    std::array<vk::DescriptorPoolSize, 1> poolSizes{ {
        { vk::DescriptorType::eUniformBuffer, totalSets },
    } };
    normalsDescPool = vk::raii::DescriptorPool(ctx.device,
            vk::DescriptorPoolCreateInfo{
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = totalSets,
                .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                .pPoolSizes = poolSizes.data(),
            });

    for (uint32_t oi = 0; oi < objectCount; oi++) {
        NormalsObject obj;
        for (uint32_t fi = 0; fi < MAX_FRAMES_IN_FLIGHT; fi++) {
            auto [buf, mem] = makeUboBuffer(ctx, sizeof(NormalsUBO));
            obj.uboMapped[fi] = mem.mapMemory(0, sizeof(NormalsUBO));
            obj.uboBuffers[fi] = std::move(buf);
            obj.uboMemory[fi] = std::move(mem);
        }

        std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *normalsDescLayout);
        obj.descriptorSets = vk::raii::DescriptorSets(ctx.device,
                vk::DescriptorSetAllocateInfo{
                    .descriptorPool = *normalsDescPool,
                    .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
                    .pSetLayouts = layouts.data(),
                });

        for (uint32_t fi = 0; fi < MAX_FRAMES_IN_FLIGHT; fi++) {
            vk::DescriptorBufferInfo uboInfo{ *obj.uboBuffers[fi], 0, sizeof(NormalsUBO) };
            std::array<vk::WriteDescriptorSet, 1> writes{ {
                { .dstSet = *obj.descriptorSets[fi],
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eUniformBuffer,
                    .pBufferInfo = &uboInfo },
            } };
            ctx.device.updateDescriptorSets(writes, {});
        }
        normalsObjects.push_back(std::move(obj));
    }
}

void SaoPipeline::updateNormalsUBO(uint32_t objectIndex, uint32_t frameIndex,
        NormalsUBO const& ubo) {
    memcpy(normalsObjects[objectIndex].uboMapped[frameIndex], &ubo, sizeof(ubo));
}

void SaoPipeline::updateUBOs(uint32_t frameIndex, glm::mat4 const& view, glm::mat4 const& proj,
        float fovYRad, float height, float nearPlane, float farPlane) {
    float projScale = (0.5f * height) / std::tan(0.5f * fovYRad);

    SaoUBO saoUbo{
        .proj = proj,
        .invProj = glm::inverse(proj),
        .radius = config.radius,
        .bias = config.bias,
        .power = config.power,
        .intensity = config.intensity,
        .projScale = projScale,
        .sampleCount = config.sampleCount,
        .spiralTurns = config.spiralTurns,
        .nearPlane = nearPlane,
        .farPlane = farPlane,
    };
    memcpy(saoUboMapped[frameIndex], &saoUbo, sizeof(saoUbo));

    // farPlaneOverEdgeDistance = -far / bilateralThreshold (matching Filament's convention)
    float farPlaneOverEdge = -farPlane / config.depthThreshold;
    for (uint32_t pi = 0; pi < 2; pi++) {
        BlurUBO blurUbo{
            .passIndex = static_cast<int>(pi),
            .farPlaneOverEdgeDistance = farPlaneOverEdge,
            .kernelRadius = config.kernelRadius,
        };
        memcpy(blurUboMapped[frameIndex][pi], &blurUbo, sizeof(blurUbo));
    }
}
