#pragma once

#include "renderer/model_asset.hpp"
#include <string>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

struct VulkanContext;
struct CommandService;

enum class TextureUsage { BaseColor, NormalMap };

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
    // Load from raw RGBA8 pixels (e.g. decoded from a GLB-embedded image).
    // Normal maps use linear UNORM data; base color uses sRGB decoding.
    TextureAtlas(VulkanContext const& ctx, CommandService const& cmds, uint8_t const* pixels,
            uint32_t width, uint32_t height, TextureUsage usage = TextureUsage::BaseColor,
            TextureSampler const& settings = {});

private:
    void uploadPixels(VulkanContext const& ctx, CommandService const& cmds, void const* pixels,
            uint32_t width, uint32_t height);
};
