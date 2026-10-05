#include "renderer/ao_pipeline.hpp"

#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "core/vulkan_logging.hpp"
#include "renderer/ao_pipeline_helpers.hpp"
#include "renderer/vertex.hpp"

#include <array>
#include <cstring>
#include <iostream>

void AoPipeline::createNormalsPass(VulkanContext const& ctx) {
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

    // normalsDescPool is created by allocateNormalsObjects once the object count is known.

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
    if (vkutil::vulkanLoggingEnabled) {
        std::cout << "AO normals prepass pipeline: created\n";
    }
}

void AoPipeline::createDepthMipPass(VulkanContext const& ctx) {
    vk::DescriptorSetLayoutBinding binding{ 0, vk::DescriptorType::eCombinedImageSampler, 1,
        vk::ShaderStageFlagBits::eFragment };
    depthMipDescLayout = vk::raii::DescriptorSetLayout(ctx.device,
            vk::DescriptorSetLayoutCreateInfo{ .bindingCount = 1, .pBindings = &binding });

    uint32_t mipPassCount = depthMipLevelCount - 1;
    vk::DescriptorPoolSize poolSize{ vk::DescriptorType::eCombinedImageSampler, mipPassCount };
    depthMipDescPool = vk::raii::DescriptorPool(ctx.device,
            vk::DescriptorPoolCreateInfo{
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = mipPassCount,
                .poolSizeCount = 1,
                .pPoolSizes = &poolSize,
            });

    std::vector<vk::DescriptorSetLayout> layouts(mipPassCount, *depthMipDescLayout);
    auto sets = vk::raii::DescriptorSets(ctx.device, vk::DescriptorSetAllocateInfo{
                                                         .descriptorPool = *depthMipDescPool,
                                                         .descriptorSetCount = mipPassCount,
                                                         .pSetLayouts = layouts.data(),
                                                     });
    for (uint32_t level = 1; level < depthMipLevelCount; ++level) {
        vk::DescriptorImageInfo previousMip{
            .sampler = *depthSampler,
            .imageView = *depthMipViews[level - 1],
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        };
        ctx.device.updateDescriptorSets(
                vk::WriteDescriptorSet{
                    .dstSet = *sets[level - 1],
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                    .pImageInfo = &previousMip,
                },
                {});
        depthMipDescSets.push_back(std::move(sets[level - 1]));
    }

    vk::DescriptorSetLayout layout = *depthMipDescLayout;
    depthMipPipeLayout = vk::raii::PipelineLayout(ctx.device, vk::PipelineLayoutCreateInfo{
                                                                  .setLayoutCount = 1,
                                                                  .pSetLayouts = &layout,
                                                              });

    auto vertMod = vkutil::createShaderModule(ctx,
            vkutil::readSpirv("shaders/compiled/fullscreen.vert.spv"));
    auto fragMod = vkutil::createShaderModule(ctx,
            vkutil::readSpirv("shaders/compiled/sao_depth_mip.frag.spv"));
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
    ps.cbInfo.attachmentCount = 0;
    ps.cbInfo.pAttachments = nullptr;
    vk::PipelineDepthStencilStateCreateInfo depthState{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
        .depthCompareOp = vk::CompareOp::eAlways,
    };
    vk::Format depthFormat = vkutil::findDepthFormat(ctx);
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
        {
            .stageCount = static_cast<uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &ps.vertexInputInfo,
            .pInputAssemblyState = &ps.iaInfo,
            .pViewportState = &ps.vpInfo,
            .pRasterizationState = &ps.rsInfo,
            .pMultisampleState = &ps.msInfo,
            .pDepthStencilState = &depthState,
            .pColorBlendState = &ps.cbInfo,
            .pDynamicState = &ps.dynInfo,
            .layout = *depthMipPipeLayout,
        },
        { .depthAttachmentFormat = depthFormat },
    };
    depthMipPipeline =
            vk::raii::Pipeline(ctx.device, nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
}

void AoPipeline::allocateNormalsObjects(VulkanContext const& ctx, uint32_t objectCount) {
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
            auto [buf, mem] = ao_detail::makeUboBuffer(ctx, sizeof(NormalsUBO));
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

void AoPipeline::updateNormalsUBO(uint32_t objectIndex, uint32_t frameIndex,
        NormalsUBO const& ubo) {
    memcpy(normalsObjects[objectIndex].uboMapped[frameIndex], &ubo, sizeof(ubo));
}
