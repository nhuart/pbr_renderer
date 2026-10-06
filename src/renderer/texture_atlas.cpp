#include "renderer/texture_atlas.hpp"
#include "core/command_service.hpp"
#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "core/vulkan_logging.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

#include <ktx.h>
#include <ktxvulkan.h>

TextureAtlas::TextureAtlas(VulkanContext const& ctx, CommandService const& cmds,
        std::string const& path) {
    ktxTexture2* kTexture = nullptr;
    KTX_error_code result = ktxTexture2_CreateFromNamedFile(path.c_str(),
            KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kTexture);
    if (result != KTX_SUCCESS) {
        throw std::runtime_error(
                "failed to load KTX2 texture: " + std::string(ktxErrorString(result)));
    }

    if (ktxTexture2_NeedsTranscoding(kTexture)) {
        result = ktxTexture2_TranscodeBasis(kTexture, KTX_TTF_BC7_RGBA, 0);
        if (result != KTX_SUCCESS) {
            ktxTexture_Destroy(
                    ktxTexture(kTexture)); // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
            throw std::runtime_error(
                    "failed to transcode KTX2 texture: " + std::string(ktxErrorString(result)));
        }
    }

    uint32_t texWidth = kTexture->baseWidth;
    uint32_t texHeight = kTexture->baseHeight;
    mipLevels = kTexture->numLevels;
    format = static_cast<vk::Format>(kTexture->vkFormat);

    ktxTexture* kBase = ktxTexture(kTexture); // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
    vk::DeviceSize totalSize = ktxTexture_GetDataSizeUncompressed(kBase);

    auto [stagingBuffer, stagingBufferMemory] = vkutil::createBuffer(ctx, totalSize,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* mappedData = stagingBufferMemory.mapMemory(0, totalSize);
    memcpy(mappedData, ktxTexture_GetData(kBase), static_cast<size_t>(totalSize));
    stagingBufferMemory.unmapMemory();

    std::vector<vk::BufferImageCopy> mipCopyRegions;
    mipCopyRegions.reserve(mipLevels);
    for (uint32_t level = 0; level < mipLevels; ++level) {
        ktx_size_t offset = 0;
        ktxTexture_GetImageOffset(kBase, level, 0, 0, &offset);
        mipCopyRegions.push_back(vk::BufferImageCopy{
            .bufferOffset      = offset,
            .bufferRowLength   = 0,
            .bufferImageHeight = 0,
            .imageSubresource  = {
                .aspectMask     = vk::ImageAspectFlagBits::eColor,
                .mipLevel       = level,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { std::max(1u, texWidth >> level), std::max(1u, texHeight >> level), 1 },
        });
    }

    ktxTexture_Destroy(kBase);

    vk::ImageUsageFlags usage =
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    auto [img, imgMem] = vkutil::createImage(ctx, texWidth, texHeight, mipLevels,
            vk::SampleCountFlagBits::e1, format, vk::ImageTiling::eOptimal, usage,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
    image = std::move(img);
    imageMemory = std::move(imgMem);

    vk::raii::CommandBuffer cmd = cmds.beginSingleTimeCommands();

    vkutil::transitionImageLayout(cmd, *image, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal, {}, vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eTransfer,
            vk::ImageAspectFlagBits::eColor, mipLevels);

    cmd.copyBufferToImage(*stagingBuffer, *image, vk::ImageLayout::eTransferDstOptimal,
            mipCopyRegions);

    vkutil::transitionImageLayout(cmd, *image, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits2::eTransferWrite,
            vk::AccessFlagBits2::eShaderRead, vk::PipelineStageFlagBits2::eTransfer,
            vk::PipelineStageFlagBits2::eFragmentShader, vk::ImageAspectFlagBits::eColor,
            mipLevels);

    cmds.endSingleTimeCommands(std::move(cmd));

    imageView = vkutil::createImageView(ctx, *image, format, vk::ImageAspectFlagBits::eColor,
            mipLevels);

    vk::PhysicalDeviceProperties properties = ctx.physicalDevice.getProperties();
    sampler = vk::raii::Sampler(ctx.device,
            vk::SamplerCreateInfo{
                .magFilter = vk::Filter::eLinear,
                .minFilter = vk::Filter::eLinear,
                .mipmapMode = vk::SamplerMipmapMode::eLinear,
                .addressModeU = vk::SamplerAddressMode::eRepeat,
                .addressModeV = vk::SamplerAddressMode::eRepeat,
                .addressModeW = vk::SamplerAddressMode::eRepeat,
                .mipLodBias = 0.0f,
                .anisotropyEnable = vk::True,
                .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
                .compareEnable = vk::False,
                .compareOp = vk::CompareOp::eAlways,
                .minLod = 0.0f,
                .maxLod = vk::LodClampNone,
                .borderColor = vk::BorderColor::eIntOpaqueBlack,
                .unnormalizedCoordinates = vk::False,
            });

    if (vkutil::vulkanLoggingEnabled) {
        std::cout << "Texture image: " << texWidth << "x" << texHeight << " (" << mipLevels
                  << " mip levels, format " << vk::to_string(format) << ") loaded\n";
        std::cout << "Texture sampler: created (max anisotropy: "
                  << properties.limits.maxSamplerAnisotropy << ")\n";
    }
}

void TextureAtlas::uploadPixels(VulkanContext const& ctx, CommandService const& cmds,
        void const* pixels, uint32_t width, uint32_t height) {
    vk::DeviceSize size = vk::DeviceSize(width) * height * 4;

    auto [stagingBuffer, stagingMemory] = vkutil::createBuffer(ctx, size,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    void* data = stagingMemory.mapMemory(0, size);
    memcpy(data, pixels, static_cast<size_t>(size));
    stagingMemory.unmapMemory();

    auto [img, imgMem] = vkutil::createImage(ctx, width, height, 1, vk::SampleCountFlagBits::e1,
            format, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
    image = std::move(img);
    imageMemory = std::move(imgMem);

    vk::raii::CommandBuffer cmd = cmds.beginSingleTimeCommands();
    vkutil::transitionImageLayout(cmd, *image, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal, {}, vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eTransfer);
    vk::BufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { width, height, 1 },
    };
    cmd.copyBufferToImage(*stagingBuffer, *image, vk::ImageLayout::eTransferDstOptimal, region);
    vkutil::transitionImageLayout(cmd, *image, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits2::eTransferWrite,
            vk::AccessFlagBits2::eShaderRead, vk::PipelineStageFlagBits2::eTransfer,
            vk::PipelineStageFlagBits2::eFragmentShader);
    cmds.endSingleTimeCommands(std::move(cmd));

    imageView = vkutil::createImageView(ctx, *image, format);
}

TextureAtlas::TextureAtlas(VulkanContext const& ctx, CommandService const& cmds,
        uint8_t const* pixels, uint32_t width, uint32_t height, bool linear) {
    mipLevels = 1;
    format = linear ? vk::Format::eR8G8B8A8Unorm : vk::Format::eR8G8B8A8Srgb;
    uploadPixels(ctx, cmds, pixels, width, height);

    vk::PhysicalDeviceProperties properties = ctx.physicalDevice.getProperties();
    sampler = vk::raii::Sampler(ctx.device,
            vk::SamplerCreateInfo{
                .magFilter = vk::Filter::eLinear,
                .minFilter = vk::Filter::eLinear,
                .mipmapMode = vk::SamplerMipmapMode::eLinear,
                .addressModeU = vk::SamplerAddressMode::eRepeat,
                .addressModeV = vk::SamplerAddressMode::eRepeat,
                .addressModeW = vk::SamplerAddressMode::eRepeat,
                .anisotropyEnable = vk::True,
                .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
                .compareEnable = vk::False,
                .compareOp = vk::CompareOp::eAlways,
                .minLod = 0.0f,
                .maxLod = 0.0f,
                .borderColor = vk::BorderColor::eIntOpaqueBlack,
            });

    if (vkutil::vulkanLoggingEnabled) {
        std::cout << (linear ? "Normal map" : "Embedded texture") << ": " << width << "x" << height
                  << " (raw pixels, " << vk::to_string(format) << ") loaded\n";
    }
}

TextureAtlas::TextureAtlas(VulkanContext const& ctx, CommandService const& cmds, uint8_t r,
        uint8_t g, uint8_t b, uint8_t a) {
    mipLevels = 1;
    format = vk::Format::eR8G8B8A8Srgb;

    uint32_t white = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
                     (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(r);
    uploadPixels(ctx, cmds, &white, 1, 1);

    sampler = vk::raii::Sampler(ctx.device, vk::SamplerCreateInfo{
                                                .magFilter = vk::Filter::eNearest,
                                                .minFilter = vk::Filter::eNearest,
                                                .mipmapMode = vk::SamplerMipmapMode::eNearest,
                                                .addressModeU = vk::SamplerAddressMode::eRepeat,
                                                .addressModeV = vk::SamplerAddressMode::eRepeat,
                                                .addressModeW = vk::SamplerAddressMode::eRepeat,
                                                .anisotropyEnable = vk::False,
                                                .maxAnisotropy = 1.0f,
                                                .compareEnable = vk::False,
                                                .compareOp = vk::CompareOp::eAlways,
                                                .minLod = 0.0f,
                                                .maxLod = 0.0f,
                                                .borderColor = vk::BorderColor::eIntOpaqueWhite,
                                            });
}
