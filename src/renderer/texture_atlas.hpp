#pragma once

#include <string>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

struct VulkanContext;
struct CommandService;

struct TextureAtlas {
    uint32_t mipLevels = 0;
    vk::Format format = vk::Format::eR8G8B8A8Srgb;
    vk::raii::Image image = nullptr;
    vk::raii::DeviceMemory imageMemory = nullptr;
    vk::raii::ImageView imageView = nullptr;
    vk::raii::Sampler sampler = nullptr;

    TextureAtlas(VulkanContext const& ctx, CommandService const& cmds, std::string const& path);
    TextureAtlas(VulkanContext const& ctx, CommandService const& cmds, uint8_t r, uint8_t g,
            uint8_t b, uint8_t a = 255);
};
