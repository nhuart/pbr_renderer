#include "renderer/tonemap_pipeline.hpp"

#include <array>

#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "core/swapchain.hpp"
#include "scene/types.hpp"

TonemapPipeline::TonemapPipeline(VulkanContext const& ctx, Swapchain const& swapchain,
        ToneMapping mode) {
    createDescriptorResources(ctx, swapchain);
    createGraphicsPipeline(ctx, swapchain, mode);
}

void TonemapPipeline::draw(vk::raii::CommandBuffer const& cmd) const {
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, *descriptorSets[0],
            {});
    cmd.draw(3, 1, 0, 0);
}

void TonemapPipeline::createDescriptorResources(VulkanContext const& ctx,
        Swapchain const& swapchain) {
    vk::DescriptorSetLayoutBinding binding{ 0, vk::DescriptorType::eCombinedImageSampler, 1,
        vk::ShaderStageFlagBits::eFragment };
    descriptorLayout = vk::raii::DescriptorSetLayout(ctx.device,
            vk::DescriptorSetLayoutCreateInfo{ .bindingCount = 1, .pBindings = &binding });
    vk::DescriptorPoolSize poolSize{ vk::DescriptorType::eCombinedImageSampler, 1 };
    descriptorPool = vk::raii::DescriptorPool(ctx.device,
            vk::DescriptorPoolCreateInfo{
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = 1,
                .poolSizeCount = 1,
                .pPoolSizes = &poolSize,
            });
    vk::DescriptorSetLayout layout = *descriptorLayout;
    descriptorSets = vk::raii::DescriptorSets(ctx.device, vk::DescriptorSetAllocateInfo{
                                                              .descriptorPool = *descriptorPool,
                                                              .descriptorSetCount = 1,
                                                              .pSetLayouts = &layout,
                                                          });
    vk::DescriptorImageInfo imageInfo{ *swapchain.hdrSampler, *swapchain.hdrImageView,
        vk::ImageLayout::eShaderReadOnlyOptimal };
    ctx.device.updateDescriptorSets(
            vk::WriteDescriptorSet{
                .dstSet = *descriptorSets[0],
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &imageInfo,
            },
            {});
    pipelineLayout = vk::raii::PipelineLayout(ctx.device, vk::PipelineLayoutCreateInfo{
                                                              .setLayoutCount = 1,
                                                              .pSetLayouts = &layout,
                                                          });
}

void TonemapPipeline::createGraphicsPipeline(VulkanContext const& ctx, Swapchain const& swapchain,
        ToneMapping mode) {
    auto vert = vkutil::createShaderModule(ctx,
            vkutil::readSpirv("shaders/compiled/fullscreen.vert.spv"));
    auto const* fragmentShader = "shaders/compiled/tonemap.frag.spv";
    if (mode == ToneMapping::None) {
        fragmentShader = "shaders/compiled/tonemap_none.frag.spv";
    }
    auto frag = vkutil::createShaderModule(ctx, vkutil::readSpirv(fragmentShader));
    std::array stages{
        vk::PipelineShaderStageCreateInfo{ .stage = vk::ShaderStageFlagBits::eVertex,
            .module = *vert,
            .pName = "main" },
        vk::PipelineShaderStageCreateInfo{ .stage = vk::ShaderStageFlagBits::eFragment,
            .module = *frag,
            .pName = "main" },
    };
    std::array dynamicStates{ vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynamicInfo{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };
    vk::PipelineVertexInputStateCreateInfo vertexInput{};
    vk::PipelineInputAssemblyStateCreateInfo assembly{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };
    vk::PipelineViewportStateCreateInfo viewport{ .viewportCount = 1, .scissorCount = 1 };
    vk::PipelineRasterizationStateCreateInfo raster{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .lineWidth = 1.0f,
    };
    vk::PipelineMultisampleStateCreateInfo multisample{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
    };
    vk::PipelineColorBlendAttachmentState colorAttachment{
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    vk::PipelineColorBlendStateCreateInfo blending{
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
    };
    vk::Format format = swapchain.surfaceFormat.format;
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
        { .stageCount = static_cast<uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &vertexInput,
            .pInputAssemblyState = &assembly,
            .pViewportState = &viewport,
            .pRasterizationState = &raster,
            .pMultisampleState = &multisample,
            .pColorBlendState = &blending,
            .pDynamicState = &dynamicInfo,
            .layout = *pipelineLayout },
        { .colorAttachmentCount = 1, .pColorAttachmentFormats = &format },
    };
    pipeline = vk::raii::Pipeline(ctx.device, nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
}
