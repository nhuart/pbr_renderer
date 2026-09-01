#pragma once

#include <utility>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

struct VulkanContext;

namespace vkutil {

[[nodiscard]] uint32_t findMemoryType(VulkanContext const& ctx, uint32_t typeFilter,
        vk::MemoryPropertyFlags props);

[[nodiscard]] std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> createBuffer(
        VulkanContext const& ctx, vk::DeviceSize size, vk::BufferUsageFlags usage,
        vk::MemoryPropertyFlags props);

void copyBuffer(VulkanContext const& ctx, vk::raii::CommandPool const& pool,
        vk::raii::Buffer& src, vk::raii::Buffer& dst, vk::DeviceSize size);

[[nodiscard]] std::pair<vk::raii::Image, vk::raii::DeviceMemory> createImage(
        VulkanContext const& ctx, uint32_t w, uint32_t h, uint32_t mipLevels,
        vk::SampleCountFlagBits samples, vk::Format format, vk::ImageTiling tiling,
        vk::ImageUsageFlags usage, vk::MemoryPropertyFlags props);

[[nodiscard]] vk::raii::ImageView createImageView(VulkanContext const& ctx, vk::Image image,
        vk::Format format,
        vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor,
        uint32_t mipLevels = 1);

[[nodiscard]] vk::Format findSupportedFormat(VulkanContext const& ctx,
        std::vector<vk::Format> const& candidates, vk::ImageTiling tiling,
        vk::FormatFeatureFlags features);

[[nodiscard]] vk::Format findDepthFormat(VulkanContext const& ctx);

void transitionImageLayout(vk::raii::CommandBuffer const& cmd, vk::Image image,
        vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::AccessFlags2 srcAccess,
        vk::AccessFlags2 dstAccess, vk::PipelineStageFlags2 srcStage,
        vk::PipelineStageFlags2 dstStage,
        vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor,
        uint32_t mipLevels = 1);

void copyBufferToImage(vk::raii::CommandBuffer const& cmd, vk::raii::Buffer const& buffer,
        vk::raii::Image const& image, uint32_t w, uint32_t h);

void generateMipmaps(VulkanContext const& ctx, vk::raii::CommandBuffer const& cmd,
        vk::raii::Image const& image, vk::Format format, int32_t texWidth, int32_t texHeight,
        uint32_t mipLevels);

}  // namespace vkutil
