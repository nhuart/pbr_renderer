#include "core/resource_allocator.hpp"
#include "core/context.hpp"

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
            std::move(ctx.device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{
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
        vk::MemoryPropertyFlags properties) {
    vk::raii::Image image(ctx.device, vk::ImageCreateInfo{
                                          .imageType = vk::ImageType::e2D,
                                          .format = format,
                                          .extent = { width, height, 1 },
                                          .mipLevels = numMipLevels,
                                          .arrayLayers = 1,
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
                .memoryTypeIndex =
                        findMemoryType(ctx, memRequirements.memoryTypeBits, properties),
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

vk::Format findSupportedFormat(VulkanContext const& ctx,
        std::vector<vk::Format> const& candidates, vk::ImageTiling tiling,
        vk::FormatFeatureFlags features) {
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
        vk::PipelineStageFlags2 dstStage, vk::ImageAspectFlags aspectFlags,
        uint32_t numMipLevels) {
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
        .subresourceRange = { aspectFlags, 0, numMipLevels, 0, 1 },
    };
    cmd.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    });
}

void copyBufferToImage(vk::raii::CommandBuffer const& cmd, vk::raii::Buffer const& buffer,
        vk::raii::Image const& image, uint32_t width, uint32_t height) {
    vk::BufferImageCopy region{
        .bufferOffset      = 0,
        .bufferRowLength   = 0,
        .bufferImageHeight = 0,
        .imageSubresource  = {
            .aspectMask     = vk::ImageAspectFlagBits::eColor,
            .mipLevel       = 0,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { width, height, 1 },
    };
    cmd.copyBufferToImage(*buffer, *image, vk::ImageLayout::eTransferDstOptimal, region);
}

void generateMipmaps(VulkanContext const& ctx, vk::raii::CommandBuffer const& cmd,
        vk::raii::Image const& image, vk::Format imageFormat, int32_t texWidth, int32_t texHeight,
        uint32_t numMipLevels) {
    vk::FormatProperties formatProperties = ctx.physicalDevice.getFormatProperties(imageFormat);
    if (!(formatProperties.optimalTilingFeatures &
                vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
        throw std::runtime_error("texture image format does not support linear blitting!");
    }

    vk::ImageMemoryBarrier2 barrier{
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image               = *image,
        .subresourceRange    = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = 1,
            .layerCount = 1,
        },
    };

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < numMipLevels; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        cmd.pipelineBarrier2(vk::DependencyInfo{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        });

        vk::ImageBlit blit{
            .srcSubresource = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel   = i - 1,
                .layerCount = 1,
            },
            .srcOffsets = std::array<vk::Offset3D, 2>{
                vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ mipWidth, mipHeight, 1 }},
            .dstSubresource = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel   = i,
                .layerCount = 1,
            },
            .dstOffsets = std::array<vk::Offset3D, 2>{
                vk::Offset3D{ 0, 0, 0 },
                vk::Offset3D{ mipWidth > 1 ? mipWidth / 2 : 1,
                    mipHeight > 1 ? mipHeight / 2 : 1, 1 },
            },
        };
        cmd.blitImage(*image, vk::ImageLayout::eTransferSrcOptimal, *image,
                vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear);

        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferRead;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
        barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        cmd.pipelineBarrier2(vk::DependencyInfo{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        });

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = numMipLevels - 1;
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
    barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    cmd.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    });
}

}  // namespace vkutil
