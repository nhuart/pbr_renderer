#include "core/application.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

#include <ktx.h>
#include <ktxvulkan.h>

void Renderer::createTextureImage() {
    ktxTexture2* kTexture = nullptr;
    KTX_error_code result = ktxTexture2_CreateFromNamedFile(TEXTURE_PATH.c_str(),
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
    textureFormat = static_cast<vk::Format>(kTexture->vkFormat);

    ktxTexture* kBase = ktxTexture(kTexture); // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)

    vk::DeviceSize totalSize = ktxTexture_GetDataSizeUncompressed(kBase);

    auto [stagingBuffer, stagingBufferMemory] = createBuffer(totalSize,
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
        .imageOffset = {0, 0, 0},
        .imageExtent = {std::max(1u, texWidth >> level), std::max(1u, texHeight >> level), 1},
    });
    }

    ktxTexture_Destroy(kBase);

    vk::ImageUsageFlags usage =
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    std::tie(textureImage, textureImageMemory) =
            createImage(texWidth, texHeight, mipLevels, vk::SampleCountFlagBits::e1, textureFormat,
                    vk::ImageTiling::eOptimal, usage, vk::MemoryPropertyFlagBits::eDeviceLocal);

    vk::raii::CommandBuffer cmd = beginSingleTimeCommands();

    transitionImageLayout(cmd, *textureImage, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal, {}, vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eTransfer,
            vk::ImageAspectFlagBits::eColor, mipLevels);

    cmd.copyBufferToImage(*stagingBuffer, *textureImage, vk::ImageLayout::eTransferDstOptimal,
            mipCopyRegions);

    transitionImageLayout(cmd, *textureImage, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits2::eTransferWrite,
            vk::AccessFlagBits2::eShaderRead, vk::PipelineStageFlagBits2::eTransfer,
            vk::PipelineStageFlagBits2::eFragmentShader, vk::ImageAspectFlagBits::eColor,
            mipLevels);

    endSingleTimeCommands(std::move(cmd));

    std::cout << "Texture image: " << texWidth << "x" << texHeight << " (" << mipLevels
              << " mip levels, format " << vk::to_string(textureFormat) << ") loaded\n";
}

void Renderer::createTextureImageView() {
    textureImageView = createImageView(*textureImage, textureFormat,
            vk::ImageAspectFlagBits::eColor, mipLevels);
}

void Renderer::createTextureSampler() {
    vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
    textureSampler =
            vk::raii::Sampler(device, vk::SamplerCreateInfo{
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
    std::cout << "Texture sampler: created (max anisotropy: "
              << properties.limits.maxSamplerAnisotropy << ")\n";
}
