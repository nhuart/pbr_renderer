#include "renderer/ao_pipeline.hpp"

#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "renderer/ao_pipeline_helpers.hpp"
#include "renderer/geometry_prepass.hpp"

void AoPipeline::createDepthMipPass(VulkanContext const& ctx) {
    vk::DescriptorSetLayoutBinding binding{ 0, vk::DescriptorType::eCombinedImageSampler, 1,
        vk::ShaderStageFlagBits::eFragment };
    depthMipDescLayout = vk::raii::DescriptorSetLayout(ctx.device,
            vk::DescriptorSetLayoutCreateInfo{ .bindingCount = 1, .pBindings = &binding });

    uint32_t mipPassCount = mGeometryPrepass.depthMipLevelCount - 1;
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
    for (uint32_t level = 1; level < mGeometryPrepass.depthMipLevelCount; ++level) {
        vk::DescriptorImageInfo previousMip{
            .sampler = *mGeometryPrepass.depthSampler,
            .imageView = *mGeometryPrepass.depthMipViews[level - 1],
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
