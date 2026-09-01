#include "core/application.hpp"

#include <stdexcept>

std::pair<vk::raii::Image, vk::raii::DeviceMemory> Renderer::createImage(
    uint32_t width, uint32_t height, uint32_t numMipLevels, vk::SampleCountFlagBits numSamples,
    vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties) {
  vk::raii::Image image(device, vk::ImageCreateInfo{
      .imageType     = vk::ImageType::e2D,
      .format        = format,
      .extent        = {width, height, 1},
      .mipLevels     = numMipLevels,
      .arrayLayers   = 1,
      .samples       = numSamples,
      .tiling        = tiling,
      .usage         = usage,
      .sharingMode   = vk::SharingMode::eExclusive,
      .initialLayout = vk::ImageLayout::eUndefined,
  });

  vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
  vk::raii::DeviceMemory imageMemory(device, vk::MemoryAllocateInfo{
      .allocationSize  = memRequirements.size,
      .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties),
  });
  image.bindMemory(*imageMemory, 0);
  return {std::move(image), std::move(imageMemory)};
}

vk::raii::ImageView Renderer::createImageView(vk::Image image, vk::Format format,
                                                              vk::ImageAspectFlags aspectFlags,
                                                              uint32_t numMipLevels) const {
  return vk::raii::ImageView(device, vk::ImageViewCreateInfo{
      .image            = image,
      .viewType         = vk::ImageViewType::e2D,
      .format           = format,
      .subresourceRange = {aspectFlags, 0, numMipLevels, 0, 1},
  });
}

vk::Format Renderer::findSupportedFormat(std::vector<vk::Format> const& candidates,
                                                         vk::ImageTiling tiling,
                                                         vk::FormatFeatureFlags features) const {
  for (vk::Format format : candidates) {
    vk::FormatProperties props = physicalDevice.getFormatProperties(format);
    if ((tiling == vk::ImageTiling::eLinear  && (props.linearTilingFeatures  & features) == features) ||
        (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features)) {
      return format;
    }
  }
  throw std::runtime_error("failed to find supported format!");
}

vk::Format Renderer::findDepthFormat() const {
  return findSupportedFormat({vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
                             vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

void Renderer::createColorResources() {
  std::tie(colorImage, colorImageMemory) =
      createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, swapChainSurfaceFormat.format,
                  vk::ImageTiling::eOptimal,
                  vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment,
                  vk::MemoryPropertyFlagBits::eDeviceLocal);
  colorImageView = createImageView(*colorImage, swapChainSurfaceFormat.format);
}

void Renderer::createDepthResources() {
  vk::Format depthFormat = findDepthFormat();
  std::tie(depthImage, depthImageMemory) = createImage(
      swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, depthFormat, vk::ImageTiling::eOptimal,
      vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
  depthImageView = createImageView(*depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth);
}

void Renderer::copyBufferToImage(vk::raii::CommandBuffer const& cmd,
                                                 vk::raii::Buffer const& buffer,
                                                 vk::raii::Image const& image,
                                                 uint32_t width, uint32_t height) {
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
      .imageOffset = {0, 0, 0},
      .imageExtent = {width, height, 1},
  };
  cmd.copyBufferToImage(*buffer, *image, vk::ImageLayout::eTransferDstOptimal, region);
}

void Renderer::generateMipmaps(vk::raii::CommandBuffer const& cmd, vk::raii::Image const& image,
                                               vk::Format imageFormat, int32_t texWidth, int32_t texHeight,
                                               uint32_t numMipLevels) {
  vk::FormatProperties formatProperties = physicalDevice.getFormatProperties(imageFormat);
  if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
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

  int32_t mipWidth  = texWidth;
  int32_t mipHeight = texHeight;

  for (uint32_t i = 1; i < numMipLevels; i++) {
    barrier.subresourceRange.baseMipLevel = i - 1;
    barrier.srcStageMask  = vk::PipelineStageFlagBits2::eTransfer;
    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    barrier.dstStageMask  = vk::PipelineStageFlagBits2::eTransfer;
    barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
    barrier.oldLayout     = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout     = vk::ImageLayout::eTransferSrcOptimal;
    cmd.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrier,
    });

    vk::ImageBlit blit{
        .srcSubresource = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .mipLevel   = i - 1,
            .layerCount = 1,
        },
        .srcOffsets     = std::array<vk::Offset3D, 2>{vk::Offset3D{0, 0, 0}, vk::Offset3D{mipWidth, mipHeight, 1}},
        .dstSubresource = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .mipLevel   = i,
            .layerCount = 1,
        },
        .dstOffsets     = std::array<vk::Offset3D, 2>{
            vk::Offset3D{0, 0, 0},
            vk::Offset3D{mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1},
        },
    };
    cmd.blitImage(*image, vk::ImageLayout::eTransferSrcOptimal, *image, vk::ImageLayout::eTransferDstOptimal, blit,
                  vk::Filter::eLinear);

    barrier.srcStageMask  = vk::PipelineStageFlagBits2::eTransfer;
    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferRead;
    barrier.dstStageMask  = vk::PipelineStageFlagBits2::eFragmentShader;
    barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
    barrier.oldLayout     = vk::ImageLayout::eTransferSrcOptimal;
    barrier.newLayout     = vk::ImageLayout::eShaderReadOnlyOptimal;
    cmd.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrier,
    });

    if (mipWidth  > 1) mipWidth  /= 2;
    if (mipHeight > 1) mipHeight /= 2;
  }

  // Transition the last mip level (never used as blit source, still in TransferDst)
  barrier.subresourceRange.baseMipLevel = numMipLevels - 1;
  barrier.srcStageMask  = vk::PipelineStageFlagBits2::eTransfer;
  barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
  barrier.dstStageMask  = vk::PipelineStageFlagBits2::eFragmentShader;
  barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
  barrier.oldLayout     = vk::ImageLayout::eTransferDstOptimal;
  barrier.newLayout     = vk::ImageLayout::eShaderReadOnlyOptimal;
  cmd.pipelineBarrier2(vk::DependencyInfo{
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers    = &barrier,
  });
}

void Renderer::transitionImageLayout(vk::raii::CommandBuffer const& cmd, vk::Image image,
                                                     vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                                                     vk::AccessFlags2 srcAccess, vk::AccessFlags2 dstAccess,
                                                     vk::PipelineStageFlags2 srcStage, vk::PipelineStageFlags2 dstStage,
                                                     vk::ImageAspectFlags aspectFlags, uint32_t numMipLevels) {
  vk::ImageMemoryBarrier2 barrier{
      .srcStageMask        = srcStage,
      .srcAccessMask       = srcAccess,
      .dstStageMask        = dstStage,
      .dstAccessMask       = dstAccess,
      .oldLayout           = oldLayout,
      .newLayout           = newLayout,
      .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
      .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
      .image               = image,
      .subresourceRange    = {aspectFlags, 0, numMipLevels, 0, 1},
  };
  cmd.pipelineBarrier2(vk::DependencyInfo{
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers    = &barrier,
  });
}
