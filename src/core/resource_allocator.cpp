#include "core/resource_allocator.hpp"
#include "core/context.hpp"

#include <bit>
#include <fstream>
#include <stdexcept>

namespace vkutil {

uint32_t findMemoryType(VulkanContext const& ctx, uint32_t typeFilter,
        vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties = ctx.physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) &&
                (memProperties.memoryTypes.at(i).propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> createBuffer(VulkanContext const& ctx,
        vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties) {
    vk::raii::Buffer buffer(ctx.device, vk::BufferCreateInfo{
                                            .size = size,
                                            .usage = usage,
                                            .sharingMode = vk::SharingMode::eExclusive,
                                        });
    vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
    vk::raii::DeviceMemory memory(ctx.device,
            vk::MemoryAllocateInfo{
                .allocationSize = memRequirements.size,
                .memoryTypeIndex = findMemoryType(ctx, memRequirements.memoryTypeBits, properties),
            });
    buffer.bindMemory(*memory, 0);
    return { std::move(buffer), std::move(memory) };
}

void copyBuffer(VulkanContext const& ctx, vk::raii::CommandPool const& pool,
        vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer, vk::DeviceSize size) {
    vk::raii::CommandBuffer cmd =
            std::move(ctx.device
                              .allocateCommandBuffers(vk::CommandBufferAllocateInfo{
                                  .commandPool = *pool,
                                  .level = vk::CommandBufferLevel::ePrimary,
                                  .commandBufferCount = 1,
                              })
                              .front());

    cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
    cmd.copyBuffer(*srcBuffer, *dstBuffer,
            vk::BufferCopy{
                .srcOffset = 0,
                .dstOffset = 0,
                .size = size,
            });
    cmd.end();

    vk::CommandBuffer cmdHandle = *cmd;
    ctx.graphicsQueue.submit(
            vk::SubmitInfo{
                .commandBufferCount = 1,
                .pCommandBuffers = &cmdHandle,
            },
            nullptr);
    ctx.graphicsQueue.waitIdle();
}

std::pair<vk::raii::Image, vk::raii::DeviceMemory> createImage(VulkanContext const& ctx,
        uint32_t width, uint32_t height, uint32_t numMipLevels, vk::SampleCountFlagBits numSamples,
        vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage,
        vk::MemoryPropertyFlags properties, uint32_t arrayLayers, vk::ImageCreateFlags flags) {
    vk::raii::Image image(ctx.device, vk::ImageCreateInfo{
                                          .flags = flags,
                                          .imageType = vk::ImageType::e2D,
                                          .format = format,
                                          .extent = { width, height, 1 },
                                          .mipLevels = numMipLevels,
                                          .arrayLayers = arrayLayers,
                                          .samples = numSamples,
                                          .tiling = tiling,
                                          .usage = usage,
                                          .sharingMode = vk::SharingMode::eExclusive,
                                          .initialLayout = vk::ImageLayout::eUndefined,
                                      });
    vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
    vk::raii::DeviceMemory imageMemory(ctx.device,
            vk::MemoryAllocateInfo{
                .allocationSize = memRequirements.size,
                .memoryTypeIndex = findMemoryType(ctx, memRequirements.memoryTypeBits, properties),
            });
    image.bindMemory(*imageMemory, 0);
    return { std::move(image), std::move(imageMemory) };
}

vk::raii::ImageView createImageView(VulkanContext const& ctx, vk::Image image, vk::Format format,
        vk::ImageAspectFlags aspectFlags, uint32_t numMipLevels) {
    return vk::raii::ImageView(ctx.device,
            vk::ImageViewCreateInfo{
                .image = image,
                .viewType = vk::ImageViewType::e2D,
                .format = format,
                .subresourceRange = { aspectFlags, 0, numMipLevels, 0, 1 },
            });
}

vk::raii::ImageView createCubemapImageView(VulkanContext const& ctx, vk::Image image,
        vk::Format format, uint32_t numMipLevels) {
    return vk::raii::ImageView(ctx.device,
            vk::ImageViewCreateInfo{
                .image = image,
                .viewType = vk::ImageViewType::eCube,
                .format = format,
                .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, numMipLevels, 0, 6 },
            });
}

vk::Format findSupportedFormat(VulkanContext const& ctx, std::vector<vk::Format> const& candidates,
        vk::ImageTiling tiling, vk::FormatFeatureFlags features) {
    for (vk::Format format: candidates) {
        vk::FormatProperties props = ctx.physicalDevice.getFormatProperties(format);
        if ((tiling == vk::ImageTiling::eLinear &&
                    (props.linearTilingFeatures & features) == features) ||
                (tiling == vk::ImageTiling::eOptimal &&
                        (props.optimalTilingFeatures & features) == features)) {
            return format;
        }
    }
    throw std::runtime_error("failed to find supported format!");
}

vk::Format findDepthFormat(VulkanContext const& ctx) {
    return findSupportedFormat(ctx,
            { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint },
            vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

void transitionImageLayout(vk::raii::CommandBuffer const& cmd, vk::Image image,
        vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::AccessFlags2 srcAccess,
        vk::AccessFlags2 dstAccess, vk::PipelineStageFlags2 srcStage,
        vk::PipelineStageFlags2 dstStage, vk::ImageAspectFlags aspectFlags, uint32_t numMipLevels,
        uint32_t arrayLayers) {
    vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image,
        .subresourceRange = { aspectFlags, 0, numMipLevels, 0, arrayLayers },
    };
    cmd.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    });
}

std::vector<char> readSpirv(std::string const& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + path);
    }
    std::vector<char> buffer(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    return buffer;
}

vk::raii::ShaderModule createShaderModule(VulkanContext const& ctx, std::vector<char> const& code) {
    return { ctx.device, vk::ShaderModuleCreateInfo{
                             .codeSize = code.size(),
                             .pCode = std::bit_cast<uint32_t const*>(code.data()),
                         } };
}

} // namespace vkutil
