#include "renderer/volumetric_pipeline.hpp"

#include <cstring>

#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "core/swapchain.hpp"
#include "renderer/geometry_prepass.hpp"
#include "renderer/shadow_pipeline.hpp"

VolumetricPipeline::VolumetricPipeline(VulkanContext const& ctx, GeometryPrepass const& depth,
        ShadowPipeline const& shadow) {
    createDescriptorResources(ctx, depth, shadow);
    createGraphicsPipeline(ctx);
}

void VolumetricPipeline::createDescriptorResources(VulkanContext const& ctx,
        GeometryPrepass const& depth, ShadowPipeline const& shadow) {
    createDescriptorLayout(ctx);
    allocateDescriptorSets(ctx);
    initializeFrameDescriptors(ctx, depth, shadow);
}

void VolumetricPipeline::createDescriptorLayout(VulkanContext const& ctx) {
    std::array<vk::DescriptorSetLayoutBinding, 3> bindings{ {
        { 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment },
        { 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
        { 2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
    } };
    descriptorLayout = vk::raii::DescriptorSetLayout(ctx.device,
            vk::DescriptorSetLayoutCreateInfo{
                .bindingCount = static_cast<uint32_t>(bindings.size()),
                .pBindings = bindings.data(),
            });
    vk::DescriptorSetLayout layout = *descriptorLayout;
    pipelineLayout = vk::raii::PipelineLayout(ctx.device, vk::PipelineLayoutCreateInfo{
                                                              .setLayoutCount = 1,
                                                              .pSetLayouts = &layout,
                                                          });
}

void VolumetricPipeline::allocateDescriptorSets(VulkanContext const& ctx) {
    std::array<vk::DescriptorPoolSize, 2> poolSizes{ {
        { vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT },
        { vk::DescriptorType::eCombinedImageSampler, 2 * MAX_FRAMES_IN_FLIGHT },
    } };
    descriptorPool = vk::raii::DescriptorPool(ctx.device,
            vk::DescriptorPoolCreateInfo{
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = MAX_FRAMES_IN_FLIGHT,
                .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                .pPoolSizes = poolSizes.data(),
            });
    std::array<vk::DescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts;
    layouts.fill(*descriptorLayout);
    descriptorSets =
            vk::raii::DescriptorSets(ctx.device, vk::DescriptorSetAllocateInfo{
                                                     .descriptorPool = *descriptorPool,
                                                     .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
                                                     .pSetLayouts = layouts.data(),
                                                 });
}

void VolumetricPipeline::initializeFrameDescriptors(VulkanContext const& ctx,
        GeometryPrepass const& depth, ShadowPipeline const& shadow) {
    vk::DescriptorImageInfo depthInfo{ *depth.depthSampler, *depth.depthMipViews[0],
        vk::ImageLayout::eShaderReadOnlyOptimal };
    vk::DescriptorImageInfo shadowInfo{ *shadow.sampler, *shadow.imageView,
        vk::ImageLayout::eShaderReadOnlyOptimal };
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        auto [buffer, memory] = vkutil::createBuffer(ctx, sizeof(VolumetricUBO),
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible |
                        vk::MemoryPropertyFlagBits::eHostCoherent);
        mapped[i] = memory.mapMemory(0, sizeof(VolumetricUBO));
        buffers[i] = std::move(buffer);
        memories[i] = std::move(memory);
        vk::DescriptorBufferInfo uniformInfo{ *buffers[i], 0, sizeof(VolumetricUBO) };
        std::array<vk::WriteDescriptorSet, 3> writes{ {
            { .dstSet = *descriptorSets[i],
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &uniformInfo },
            { .dstSet = *descriptorSets[i],
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &depthInfo },
            { .dstSet = *descriptorSets[i],
                .dstBinding = 2,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &shadowInfo },
        } };
        ctx.device.updateDescriptorSets(writes, {});
    }
}

void VolumetricPipeline::createGraphicsPipeline(VulkanContext const& ctx) {
    auto vert = vkutil::createShaderModule(ctx,
            vkutil::readSpirv("shaders/compiled/fullscreen.vert.spv"));
    auto frag = vkutil::createShaderModule(ctx,
            vkutil::readSpirv("shaders/compiled/volumetric.frag.spv"));
    std::array stages{ vk::PipelineShaderStageCreateInfo{ .stage = vk::ShaderStageFlagBits::eVertex,
                           .module = *vert,
                           .pName = "main" },
        vk::PipelineShaderStageCreateInfo{ .stage = vk::ShaderStageFlagBits::eFragment,
            .module = *frag,
            .pName = "main" } };
    std::array dynamicStates{ vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynamicInfo{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };
    vk::PipelineVertexInputStateCreateInfo vertexInfo{};
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
    vk::PipelineColorBlendAttachmentState blendAttachment{
        .blendEnable = vk::True,
        .srcColorBlendFactor = vk::BlendFactor::eOne,
        .dstColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eZero,
        .dstAlphaBlendFactor = vk::BlendFactor::eOne,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB,
    };
    vk::PipelineColorBlendStateCreateInfo blend{
        .attachmentCount = 1,
        .pAttachments = &blendAttachment,
    };
    vk::Format colorFormat = Swapchain::HDR_COLOR_FORMAT;
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
        { .stageCount = static_cast<uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &vertexInfo,
            .pInputAssemblyState = &assembly,
            .pViewportState = &viewport,
            .pRasterizationState = &raster,
            .pMultisampleState = &multisample,
            .pColorBlendState = &blend,
            .pDynamicState = &dynamicInfo,
            .layout = *pipelineLayout },
        { .colorAttachmentCount = 1, .pColorAttachmentFormats = &colorFormat },
    };
    pipeline = vk::raii::Pipeline(ctx.device, nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
}

void VolumetricPipeline::update(uint32_t frame, VolumetricUBO const& ubo) {
    std::memcpy(mapped[frame], &ubo, sizeof(ubo));
}

void VolumetricPipeline::draw(vk::raii::CommandBuffer const& cmd, uint32_t frame) const {
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0,
            *descriptorSets[frame], {});
    cmd.draw(3, 1, 0, 0);
}
