#include "core/command_service.hpp"
#include "core/context.hpp"
#include "scene/types.hpp"

CommandService::CommandService(VulkanContext const& ctx)
        : ctx_(&ctx) {
    commandPool = vk::raii::CommandPool(ctx.device,
            vk::CommandPoolCreateInfo{
                .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                .queueFamilyIndex = ctx.graphicsQueueFamilyIndex,
            });
}

void CommandService::allocateCommandBuffers(VulkanContext const& ctx) {
    commandBuffers =
            vk::raii::CommandBuffers(ctx.device, vk::CommandBufferAllocateInfo{
                                                     .commandPool = *commandPool,
                                                     .level = vk::CommandBufferLevel::ePrimary,
                                                     .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
                                                 });
}

void CommandService::allocateComputeCommandBuffers(VulkanContext const& ctx) {
    computeCommandBuffers =
            vk::raii::CommandBuffers(ctx.device, vk::CommandBufferAllocateInfo{
                                                     .commandPool = *commandPool,
                                                     .level = vk::CommandBufferLevel::ePrimary,
                                                     .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
                                                 });
}

vk::raii::CommandBuffer CommandService::beginSingleTimeCommands() const {
    vk::raii::CommandBuffer cmd = std::move(vk::raii::CommandBuffers(ctx_->device,
            vk::CommandBufferAllocateInfo{
                .commandPool = *commandPool,
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = 1,
            })
                                                    .front());
    cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
    return cmd;
}

void CommandService::endSingleTimeCommands(vk::raii::CommandBuffer cmd) const {
    cmd.end();
    vk::CommandBuffer cmdHandle = *cmd;
    ctx_->graphicsQueue.submit(
            vk::SubmitInfo{
                .commandBufferCount = 1,
                .pCommandBuffers = &cmdHandle,
            },
            nullptr);
    ctx_->graphicsQueue.waitIdle();
}
