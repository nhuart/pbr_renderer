#pragma once

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "renderer/gpu_types.hpp"

struct VulkanContext;
struct CommandService;

struct LightBuffer {
    vk::raii::Buffer buffer{ nullptr };
    vk::raii::DeviceMemory memory{ nullptr };
    void* mapped = nullptr;

    LightBuffer() = default;
    LightBuffer(VulkanContext const& ctx, LightUBO const& data);
};
