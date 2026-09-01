#include "core/application.hpp"

#include <limits>

void Renderer::createCommandPool() {
    commandPool = vk::raii::CommandPool(device,
            vk::CommandPoolCreateInfo{
                .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                .queueFamilyIndex = graphicsQueueFamilyIndex,
            });
}

vk::raii::CommandBuffer Renderer::beginSingleTimeCommands() const {
    vk::raii::CommandBuffer cmd = std::move(vk::raii::CommandBuffers(device,
            vk::CommandBufferAllocateInfo{
                .commandPool = *commandPool,
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = 1,
            })
                                                    .front());
    cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
    return cmd;
}

void Renderer::endSingleTimeCommands(vk::raii::CommandBuffer cmd) const {
    cmd.end();
    vk::CommandBuffer cmdHandle = *cmd;
    graphicsQueue.submit(
            vk::SubmitInfo{
                .commandBufferCount = 1,
                .pCommandBuffers = &cmdHandle,
            },
            nullptr);
    graphicsQueue.waitIdle();
}

void Renderer::createCommandBuffers() {
    commandBuffers =
            vk::raii::CommandBuffers(device, vk::CommandBufferAllocateInfo{
                                                 .commandPool = *commandPool,
                                                 .level = vk::CommandBufferLevel::ePrimary,
                                                 .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
                                             });
}

void Renderer::createComputeCommandBuffers() {
    computeCommandBuffers =
            vk::raii::CommandBuffers(device, vk::CommandBufferAllocateInfo{
                                                 .commandPool = *commandPool,
                                                 .level = vk::CommandBufferLevel::ePrimary,
                                                 .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
                                             });
}

void Renderer::recordComputeCommandBuffer(uint32_t frameIdx) {
    auto const& cmd = computeCommandBuffers[frameIdx];
    cmd.begin({});
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *computePipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *computePipelineLayout, 0,
            *computeDescriptorSets[frameIdx], {});
    cmd.dispatch(PARTICLE_COUNT / 256, 1, 1);
    cmd.end();
}

void Renderer::recordCommandBuffer(uint32_t imageIndex) {
    auto const& cmd = commandBuffers[frameIndex];
    cmd.begin({});

    transitionImageLayout(cmd, swapChainImages[imageIndex], vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, {},
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput);

    transitionImageLayout(cmd, *colorImage, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, {},
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput);

    transitionImageLayout(cmd, *depthImage, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                    vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                    vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::ImageAspectFlagBits::eDepth);

    vk::ClearValue clearColor = vk::ClearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f };
    vk::RenderingAttachmentInfo colorAttachmentInfo{
        .imageView = *colorImageView,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .resolveMode = vk::ResolveModeFlagBits::eAverage,
        .resolveImageView = *swapChainImageViews[imageIndex],
        .resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = clearColor,
    };
    vk::ClearValue clearDepth = vk::ClearDepthStencilValue{ 1.0f, 0 };
    vk::RenderingAttachmentInfo depthAttachmentInfo{
        .imageView = *depthImageView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = clearDepth,
    };
    vk::RenderingInfo renderingInfo{
        .renderArea = { .offset = { 0, 0 }, .extent = swapChainExtent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentInfo,
        .pDepthAttachment = &depthAttachmentInfo,
    };

    cmd.beginRendering(renderingInfo);

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);
    cmd.bindVertexBuffers(0, *vertexBuffer, { vk::DeviceSize{ 0 } });
    cmd.bindIndexBuffer(*indexBuffer, 0, vk::IndexType::eUint32);

    vk::Viewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(swapChainExtent.width),
        .height = static_cast<float>(swapChainExtent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, vk::Rect2D{ .offset = { 0, 0 }, .extent = swapChainExtent });

    for (auto const& obj: gameObjects) {
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0,
                *obj.descriptorSets[frameIndex], {});
        cmd.drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
    }

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *particlePipeline);
    cmd.bindVertexBuffers(0, *shaderStorageBuffers[frameIndex], { vk::DeviceSize{ 0 } });
    cmd.draw(PARTICLE_COUNT, 1, 0, 0);

    cmd.endRendering();

    transitionImageLayout(cmd, swapChainImages[imageIndex],
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite, {},
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eBottomOfPipe);

    cmd.end();
}
