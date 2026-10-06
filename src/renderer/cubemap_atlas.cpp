#include "renderer/cubemap_atlas.hpp"
#include "core/command_service.hpp"
#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "core/vulkan_logging.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

#include <ktx.h>

CubemapAtlas::CubemapAtlas(VulkanContext const& ctx, CommandService const& cmds,
        std::string const& path) {
    // Detect KTX version from the identifier bytes (byte 7: '1' or '2')
    ktxTexture* kBase = nullptr;
    bool isKtx1 = false;
    {
        FILE* f = fopen(path.c_str(), "rb");
        if (f) {
            uint8_t id[12] = {};
            fread(id, 1, 12, f);
            fclose(f);
            isKtx1 = (id[5] == '1');
        }
    }

    vk::Format uploadFormat;

    if (isKtx1) {
        ktxTexture1* kt1 = nullptr;
        KTX_error_code result = ktxTexture1_CreateFromNamedFile(path.c_str(),
                KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kt1);
        if (result != KTX_SUCCESS) {
            throw std::runtime_error(
                    "failed to load cubemap KTX1: " + std::string(ktxErrorString(result)));
        }
        // Map GL internal format to VkFormat for the formats cmgen produces
        vk::Format glToVk = vk::Format::eUndefined;
        switch (kt1->glInternalformat) {
            case 0x8C3A:
                glToVk = vk::Format::eB10G11R11UfloatPack32;
                break; // GL_R11F_G11F_B10F
            case 0x881B:
                glToVk = vk::Format::eR16G16B16Sfloat;
                break; // GL_RGB16F
            case 0x881A:
                glToVk = vk::Format::eR16G16B16A16Sfloat;
                break; // GL_RGBA16F
            case 0x1907:
                glToVk = vk::Format::eR8G8B8Unorm;
                break; // GL_RGB
            case 0x1908:
                glToVk = vk::Format::eR8G8B8A8Unorm;
                break; // GL_RGBA
            default:
                ktxTexture_Destroy(ktxTexture(kt1));
                throw std::runtime_error("unsupported KTX1 GL internal format: " +
                                         std::to_string(kt1->glInternalformat));
        }
        uploadFormat = glToVk;
        kBase = ktxTexture(kt1);
    } else {
        ktxTexture2* kt2 = nullptr;
        KTX_error_code result = ktxTexture2_CreateFromNamedFile(path.c_str(),
                KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kt2);
        if (result != KTX_SUCCESS) {
            throw std::runtime_error(
                    "failed to load cubemap KTX2: " + std::string(ktxErrorString(result)));
        }
        if (ktxTexture2_NeedsTranscoding(kt2)) {
            result = ktxTexture2_TranscodeBasis(kt2, KTX_TTF_BC7_RGBA, 0);
            if (result != KTX_SUCCESS) {
                ktxTexture_Destroy(ktxTexture(kt2));
                throw std::runtime_error(
                        "failed to transcode cubemap: " + std::string(ktxErrorString(result)));
            }
        }
        uploadFormat = static_cast<vk::Format>(kt2->vkFormat);
        kBase = ktxTexture(kt2);
    }

    uint32_t texWidth = kBase->baseWidth;
    uint32_t texHeight = kBase->baseHeight;
    mipLevels = kBase->numLevels;
    uint32_t numFaces = kBase->numFaces;
    format = uploadFormat;
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

    if (vkutil::vulkanLoggingEnabled) {
        std::cout << "Cubemap: " << texWidth << "x" << texHeight << " (" << mipLevels
                  << " mip levels, " << numFaces << " faces, format " << vk::to_string(format)
                  << ") loaded\n";
    }
}
