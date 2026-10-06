#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "core/vulkan_logging.hpp"
#include "renderer/ao_pipeline.hpp"
#include "renderer/ao_pipeline_helpers.hpp"

#include <array>
#include <cstring>
#include <iostream>
void AoPipeline::createAoPass(VulkanContext const& ctx) {
    constexpr uint32_t frameCount = MAX_FRAMES_IN_FLIGHT;

    // bindings: 0=selected AO UBO, 1=depthSampler, 2=normalSampler
    std::array<vk::DescriptorSetLayoutBinding, 3> bindings{ {
        { 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment },
        { 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
        { 2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
    } };
    aoDescLayout = vk::raii::DescriptorSetLayout(ctx.device,
            vk::DescriptorSetLayoutCreateInfo{
                .bindingCount = static_cast<uint32_t>(bindings.size()),
                .pBindings = bindings.data(),
            });

    std::array<vk::DescriptorPoolSize, 2> poolSizes{ {
        { vk::DescriptorType::eUniformBuffer, frameCount },
        { vk::DescriptorType::eCombinedImageSampler, frameCount * 2 },
    } };
    aoDescPool = vk::raii::DescriptorPool(ctx.device,
            vk::DescriptorPoolCreateInfo{
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = frameCount,
                .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                .pPoolSizes = poolSizes.data(),
            });

    for (uint32_t i = 0; i < frameCount; i++) {
        auto [buf, mem] = ao_detail::makeUboBuffer(ctx, sizeof(SaoUBO));
        aoUboMapped.push_back(mem.mapMemory(0, sizeof(SaoUBO)));
        aoUboBuffers.push_back(std::move(buf));
        aoUboMemory.push_back(std::move(mem));
    }

    std::vector<vk::DescriptorSetLayout> layouts(frameCount, *aoDescLayout);
    auto sets = vk::raii::DescriptorSets(ctx.device, vk::DescriptorSetAllocateInfo{
                                                         .descriptorPool = *aoDescPool,
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
        vk::DescriptorBufferInfo uboInfo{ *aoUboBuffers[i], 0, sizeof(SaoUBO) };
        std::array<vk::WriteDescriptorSet, 3> writes{ {
            { .dstSet = *sets[i],
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &uboInfo },
            { .dstSet = *sets[i],
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &depthInfo },
            { .dstSet = *sets[i],
                .dstBinding = 2,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &normalsInfo },
        } };
        ctx.device.updateDescriptorSets(writes, {});
        aoDescSets.push_back(std::move(sets[i]));
    }

    vk::DescriptorSetLayout dslHandle = *aoDescLayout;
    aoPipeLayout = vk::raii::PipelineLayout(ctx.device, vk::PipelineLayoutCreateInfo{
                                                            .setLayoutCount = 1,
                                                            .pSetLayouts = &dslHandle,
                                                        });

    auto vertCode = vkutil::readSpirv("shaders/compiled/fullscreen.vert.spv");
    bool gtao = std::holds_alternative<GtaoConfig>(config);
    auto fragCode = vkutil::readSpirv(
            gtao ? "shaders/compiled/gtao.frag.spv" : "shaders/compiled/sao.frag.spv");
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

    auto ps = ao_detail::makeFullscreenPipelineState();
    constexpr vk::Format aoFormat = vk::Format::eR8G8B8A8Unorm;

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
        {
            .stageCount = static_cast<uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &ps.vertexInputInfo,
            .pInputAssemblyState = &ps.iaInfo,
            .pViewportState = &ps.vpInfo,
            .pRasterizationState = &ps.rsInfo,
            .pMultisampleState = &ps.msInfo,
            .pColorBlendState = &ps.cbInfo,
            .pDynamicState = &ps.dynInfo,
            .layout = *aoPipeLayout,
        },
        {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &aoFormat,
        },
    };
    aoPipeline =
            vk::raii::Pipeline(ctx.device, nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
    if (vkutil::vulkanLoggingEnabled) {
        std::cout << (gtao ? "GTAO" : "SAO") << " occlusion pipeline: created\n";
    }
}


void AoPipeline::createBlurPass(VulkanContext const& ctx) {
    constexpr uint32_t frameCount = MAX_FRAMES_IN_FLIGHT;
    constexpr uint32_t passesPerFrame = 2; // H + V
    int kernelRadius = std::visit([](auto const& cfg) { return cfg.kernelRadius; }, config);
    int sampleStride = std::holds_alternative<SaoConfig>(config) ? 2 : 1;

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
            auto [buf, mem] = ao_detail::makeUboBuffer(ctx, sizeof(BlurUBO));
            blurUboMapped[fi][pi] = mem.mapMemory(0, sizeof(BlurUBO));
            blurUboBuffers[fi][pi] = std::move(buf);
            blurUboMemory[fi][pi] = std::move(mem);

            BlurUBO blurUbo{
                .passIndex = static_cast<int>(pi),
                .farPlaneOverEdgeDistance = 0.0f, // updated each frame via updateUBOs
                .kernelRadius = kernelRadius,
                .sampleStride = sampleStride,
            };
            memcpy(blurUboMapped[fi][pi], &blurUbo, sizeof(blurUbo));
        }

        // Allocate 2 descriptor sets for this frame
        std::vector<vk::DescriptorSetLayout> layouts(passesPerFrame, *blurDescLayout);
        auto sets = vk::raii::DescriptorSets(ctx.device, vk::DescriptorSetAllocateInfo{
                                                             .descriptorPool = *blurDescPool,
                                                             .descriptorSetCount = passesPerFrame,
                                                             .pSetLayouts = layouts.data(),
                                                         });

        // Pass 0: reads aoRaw (binding 1 — depth packed in GB)
        // Pass 1: reads aoBlur (binding 1 — depth packed in GB)
        for (uint32_t pi = 0; pi < passesPerFrame; pi++) {
            vk::ImageView aoView = (pi == 0) ? *aoRawView : *aoBlurView;
            vk::Sampler aoSamp = (pi == 0) ? *aoRawSampler : *aoBlurSampler;
            vk::DescriptorImageInfo aoInfo{
                .sampler = aoSamp,
                .imageView = aoView,
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            };

            vk::DescriptorBufferInfo uboInfo{ *blurUboBuffers[fi][pi], 0, sizeof(BlurUBO) };
            std::array<vk::WriteDescriptorSet, 2> writes{ {
                { .dstSet = *sets[pi],
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eUniformBuffer,
                    .pBufferInfo = &uboInfo },
                { .dstSet = *sets[pi],
                    .dstBinding = 1,
                    .descriptorCount = 1,
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

    auto ps = ao_detail::makeFullscreenPipelineState();
    constexpr vk::Format aoFormat = vk::Format::eR8G8B8A8Unorm;

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
        {
            .stageCount = static_cast<uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &ps.vertexInputInfo,
            .pInputAssemblyState = &ps.iaInfo,
            .pViewportState = &ps.vpInfo,
            .pRasterizationState = &ps.rsInfo,
            .pMultisampleState = &ps.msInfo,
            .pColorBlendState = &ps.cbInfo,
            .pDynamicState = &ps.dynInfo,
            .layout = *blurPipeLayout,
        },
        {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &aoFormat,
        },
    };
    blurPipeline =
            vk::raii::Pipeline(ctx.device, nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
    if (vkutil::vulkanLoggingEnabled) {
        std::cout << "AO blur pipeline: created\n";
    }
}
