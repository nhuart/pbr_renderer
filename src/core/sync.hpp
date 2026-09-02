#pragma once

#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

struct VulkanContext;

struct SyncObjects {
    std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
    std::vector<vk::raii::Fence> inFlightFences;
    std::vector<vk::raii::Semaphore> computeFinishedSemaphores;
    std::vector<vk::raii::Fence> computeInFlightFences;

    SyncObjects(VulkanContext const& ctx, uint32_t swapImageCount, bool hasParticles);
    void recreatePresent(VulkanContext const& ctx, uint32_t newSwapImageCount);
};
