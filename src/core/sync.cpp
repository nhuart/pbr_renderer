#include "core/sync.hpp"
#include "core/config.hpp"
#include "core/context.hpp"

SyncObjects::SyncObjects(VulkanContext const& ctx, uint32_t swapImageCount, bool hasParticles) {
    for (uint32_t i = 0; i < swapImageCount; i++) {
        renderFinishedSemaphores.emplace_back(ctx.device, vk::SemaphoreCreateInfo{});
    }
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        presentCompleteSemaphores.emplace_back(ctx.device, vk::SemaphoreCreateInfo{});
        inFlightFences.emplace_back(ctx.device,
                vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
        if (hasParticles) {
            computeFinishedSemaphores.emplace_back(ctx.device, vk::SemaphoreCreateInfo{});
            computeInFlightFences.emplace_back(ctx.device,
                    vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
        }
    }
}

void SyncObjects::recreatePresent(VulkanContext const& ctx, uint32_t newSwapImageCount) {
    renderFinishedSemaphores.clear();
    for (uint32_t i = 0; i < newSwapImageCount; i++) {
        renderFinishedSemaphores.emplace_back(ctx.device, vk::SemaphoreCreateInfo{});
    }
}
