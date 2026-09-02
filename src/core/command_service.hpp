#pragma once

#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

struct VulkanContext;

struct CommandService {
    vk::raii::CommandPool                commandPool     = nullptr;
    std::vector<vk::raii::CommandBuffer> commandBuffers;
    std::vector<vk::raii::CommandBuffer> computeCommandBuffers;

    explicit CommandService(VulkanContext const& ctx);
    void allocateCommandBuffers(VulkanContext const& ctx);
    void allocateComputeCommandBuffers(VulkanContext const& ctx);
    [[nodiscard]] vk::raii::CommandBuffer beginSingleTimeCommands() const;
    void endSingleTimeCommands(vk::raii::CommandBuffer cmd) const;

private:
    VulkanContext const* mCtx = nullptr;
};
