#pragma once

#include <string>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

struct VulkanContext;
struct CommandService;

struct CubemapAtlas {
    uint32_t mipLevels = 0;
    vk::Format format = vk::Format::eUndefined;
    vk::raii::Image image{ nullptr };
    vk::raii::DeviceMemory imageMemory{ nullptr };
    vk::raii::ImageView imageView{ nullptr };
    vk::raii::Sampler sampler{ nullptr };

    CubemapAtlas(VulkanContext const& ctx, CommandService const& cmds, std::string const& path);
};
