#include "renderer/geometry_prepass.hpp"

#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "core/swapchain.hpp"
#include "core/vulkan_logging.hpp"
#include "renderer/vertex.hpp"

#include <array>
#include <cstring>
#include <iostream>

GeometryPrepass::GeometryPrepass(VulkanContext const& ctx, Swapchain const& swapchain,
        uint32_t depthMipLevels)
        : depthMipLevelCount(depthMipLevels) {
    createImages(ctx, swapchain.extent);
    createNormalsPass(ctx);
}

void GeometryPrepass::createImages(VulkanContext const& ctx, vk::Extent2D extent) {
    vk::Format depthFormat = vkutil::findDepthFormat(ctx);
    auto [depth, depthAllocation] = vkutil::createImage(ctx, extent.width, extent.height,
            depthMipLevelCount, vk::SampleCountFlagBits::e1, depthFormat, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
    depthImage = std::move(depth);
    depthMemory = std::move(depthAllocation);
    depthView = vkutil::createImageView(ctx, *depthImage, depthFormat,
            vk::ImageAspectFlagBits::eDepth, depthMipLevelCount);
    for (uint32_t level = 0; level < depthMipLevelCount; ++level) {
        depthMipViews.emplace_back(ctx.device,
                vk::ImageViewCreateInfo{
                    .image = *depthImage,
                    .viewType = vk::ImageViewType::e2D,
                    .format = depthFormat,
                    .subresourceRange = { vk::ImageAspectFlagBits::eDepth, level, 1, 0, 1 },
                });
    }
    depthSampler = vkutil::createNearestSampler(ctx, depthMipLevelCount);

    constexpr vk::Format normalsFormat = vk::Format::eR8G8B8A8Unorm;
    auto [normals, normalsAllocation] = vkutil::createImage(ctx, extent.width, extent.height, 1,
            vk::SampleCountFlagBits::e1, normalsFormat, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
                    vk::ImageUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
    normalsImage = std::move(normals);
    normalsMemory = std::move(normalsAllocation);
    normalsView = vkutil::createImageView(ctx, *normalsImage, normalsFormat);
    normalsSampler = vkutil::createNearestSampler(ctx);
}

void GeometryPrepass::createNormalsPass(VulkanContext const& ctx) {
    // Descriptor set layout: binding 0 = UniformBufferObject (vert+frag)
    std::array<vk::DescriptorSetLayoutBinding, 1> bindings{ {
        { .binding = 0,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment },
    } };
    normalsDescLayout = vk::raii::DescriptorSetLayout(ctx.device,
            vk::DescriptorSetLayoutCreateInfo{
                .bindingCount = static_cast<uint32_t>(bindings.size()),
                .pBindings = bindings.data(),
            });

    // The descriptor pool is sized when the scene's object count is known.

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
        .cullMode = vk::CullModeFlagBits::eBack,
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

    constexpr vk::Format normalsColorFormat = vk::Format::eR8G8B8A8Unorm;
    vk::Format normalsDepthFormat = vkutil::findDepthFormat(ctx);

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
            .pColorAttachmentFormats = &normalsColorFormat,
            .depthAttachmentFormat = normalsDepthFormat,
        },
    };
    normalsPipeline =
            vk::raii::Pipeline(ctx.device, nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
    rsInfo.cullMode = vk::CullModeFlagBits::eNone;
    doubleSidedNormalsPipeline =
            vk::raii::Pipeline(ctx.device, nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
    if (vkutil::vulkanLoggingEnabled) {
        std::cout << "Geometry prepass pipeline: created\n";
    }
}

void GeometryPrepass::allocateObjects(VulkanContext const& ctx, uint32_t objectCount) {
    normalsObjects.clear();
    if (objectCount == 0) {
        return;
    }

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
            auto [buf, mem] = vkutil::createBuffer(ctx, sizeof(NormalsUBO),
                    vk::BufferUsageFlagBits::eUniformBuffer,
                    vk::MemoryPropertyFlagBits::eHostVisible |
                            vk::MemoryPropertyFlagBits::eHostCoherent);
            obj.uboMapped[fi] = mem.mapMemory(0, sizeof(NormalsUBO));
            obj.uboBuffers[fi] = std::move(buf);
            obj.uboMemory[fi] = std::move(mem);
        }

        std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *normalsDescLayout);
        obj.descriptorSets =
                vk::raii::DescriptorSets(ctx.device, vk::DescriptorSetAllocateInfo{
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

void GeometryPrepass::updateObjectUBO(uint32_t objectIndex, uint32_t frameIndex,
        NormalsUBO const& ubo) {
    memcpy(normalsObjects[objectIndex].uboMapped[frameIndex], &ubo, sizeof(ubo));
}
