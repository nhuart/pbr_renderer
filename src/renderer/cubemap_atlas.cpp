#include "renderer/cubemap_atlas.hpp"
#include "core/command_service.hpp"
#include "core/context.hpp"
#include "core/resource_allocator.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

#include <ktx.h>
#include <ktxvulkan.h>

CubemapAtlas::CubemapAtlas(VulkanContext const& ctx, CommandService const& cmds,
        std::string const& path) {
    ktxTexture2* kTexture = nullptr;
    KTX_error_code result = ktxTexture2_CreateFromNamedFile(path.c_str(),
            KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kTexture);
    if (result != KTX_SUCCESS) {
        throw std::runtime_error(
                "failed to load cubemap KTX2: " + std::string(ktxErrorString(result)));
    }

    if (ktxTexture2_NeedsTranscoding(kTexture)) {
        result = ktxTexture2_TranscodeBasis(kTexture, KTX_TTF_BC7_RGBA, 0);
        if (result != KTX_SUCCESS) {
            ktxTexture_Destroy(ktxTexture(kTexture));
            throw std::runtime_error(
                    "failed to transcode cubemap: " + std::string(ktxErrorString(result)));
        }
    }

    uint32_t texWidth = kTexture->baseWidth;
    uint32_t texHeight = kTexture->baseHeight;
    mipLevels = kTexture->numLevels;
    uint32_t numFaces = kTexture->numFaces;
    format = static_cast<vk::Format>(kTexture->vkFormat);

    ktxTexture* kBase = ktxTexture(kTexture);
    vk::DeviceSize totalSize = ktxTexture_GetDataSize(kBase);

    auto [stagingBuffer, stagingBufferMemory] = vkutil::createBuffer(ctx, totalSize,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    void* mappedData = stagingBufferMemory.mapMemory(0, totalSize);
    memcpy(mappedData, ktxTexture_GetData(kBase), static_cast<size_t>(totalSize));
    stagingBufferMemory.unmapMemory();

    // Build one copy region per face per mip level
    std::vector<vk::BufferImageCopy> copyRegions;
    copyRegions.reserve(numFaces * mipLevels);
    for (uint32_t face = 0; face < numFaces; ++face) {
        for (uint32_t level = 0; level < mipLevels; ++level) {
            ktx_size_t offset = 0;
            ktxTexture_GetImageOffset(kBase, level, 0, face, &offset);
            copyRegions.push_back(vk::BufferImageCopy{
                .bufferOffset      = offset,
                .bufferRowLength   = 0,
                .bufferImageHeight = 0,
                .imageSubresource  = {
                    .aspectMask     = vk::ImageAspectFlagBits::eColor,
                    .mipLevel       = level,
                    .baseArrayLayer = face,
                    .layerCount     = 1,
                },
                .imageOffset = { 0, 0, 0 },
                .imageExtent = { std::max(1u, texWidth >> level), std::max(1u, texHeight >> level), 1 },
            });
        }
    }

    ktxTexture_Destroy(kBase);

    auto [img, imgMem] = vkutil::createImage(ctx, texWidth, texHeight, mipLevels,
            vk::SampleCountFlagBits::e1, format, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal, 6, vk::ImageCreateFlagBits::eCubeCompatible);
    image = std::move(img);
    imageMemory = std::move(imgMem);

    vk::raii::CommandBuffer cmd = cmds.beginSingleTimeCommands();

    vkutil::transitionImageLayout(cmd, *image, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal, {}, vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eTransfer,
            vk::ImageAspectFlagBits::eColor, mipLevels, 6);

    cmd.copyBufferToImage(*stagingBuffer, *image, vk::ImageLayout::eTransferDstOptimal,
            copyRegions);

    vkutil::transitionImageLayout(cmd, *image, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits2::eTransferWrite,
            vk::AccessFlagBits2::eShaderRead, vk::PipelineStageFlagBits2::eTransfer,
            vk::PipelineStageFlagBits2::eFragmentShader, vk::ImageAspectFlagBits::eColor, mipLevels,
            6);

    cmds.endSingleTimeCommands(std::move(cmd));

    imageView = vkutil::createCubemapImageView(ctx, *image, format, mipLevels);

    sampler =
            vk::raii::Sampler(ctx.device, vk::SamplerCreateInfo{
                                              .magFilter = vk::Filter::eLinear,
                                              .minFilter = vk::Filter::eLinear,
                                              .mipmapMode = vk::SamplerMipmapMode::eLinear,
                                              .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                                              .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                                              .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                                              .mipLodBias = 0.0f,
                                              .anisotropyEnable = vk::False,
                                              .maxAnisotropy = 1.0f,
                                              .compareEnable = vk::False,
                                              .compareOp = vk::CompareOp::eAlways,
                                              .minLod = 0.0f,
                                              .maxLod = vk::LodClampNone,
                                              .borderColor = vk::BorderColor::eIntOpaqueBlack,
                                          });

    std::cout << "Cubemap: " << texWidth << "x" << texHeight << " (" << mipLevels << " mip levels, "
              << numFaces << " faces, format " << vk::to_string(format) << ") loaded\n";
}
