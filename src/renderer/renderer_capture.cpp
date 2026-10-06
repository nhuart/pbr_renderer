#include "renderer/renderer.hpp"

#include "core/resource_allocator.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

#include <stb_image_write.h>

void Renderer::captureScreenshot(uint32_t imageIndex) {
    uint32_t width = mSwapchain->extent.width;
    uint32_t height = mSwapchain->extent.height;
    vk::DeviceSize bufferSize = vk::DeviceSize(width) * height * 4;

    auto [readbackBuffer, readbackMemory] = vkutil::createBuffer(*mCtx, bufferSize,
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);

    vk::raii::CommandBuffer cmd =
            std::move(mCtx->device
                              .allocateCommandBuffers(vk::CommandBufferAllocateInfo{
                                  .commandPool = *mCmds->commandPool,
                                  .level = vk::CommandBufferLevel::ePrimary,
                                  .commandBufferCount = 1,
                              })
                              .front());

    cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

    vk::Image srcImage = mSwapchain->images[imageIndex];

    vkutil::transitionImageLayout(cmd, srcImage, vk::ImageLayout::ePresentSrcKHR,
            vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eNone,
            vk::AccessFlagBits2::eTransferRead, vk::PipelineStageFlagBits2::eBottomOfPipe,
            vk::PipelineStageFlagBits2::eTransfer);

    vk::BufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { width, height, 1 },
    };
    cmd.copyImageToBuffer(srcImage, vk::ImageLayout::eTransferSrcOptimal, *readbackBuffer, region);

    vkutil::transitionImageLayout(cmd, srcImage, vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageLayout::ePresentSrcKHR, vk::AccessFlagBits2::eTransferRead,
            vk::AccessFlagBits2::eNone, vk::PipelineStageFlagBits2::eTransfer,
            vk::PipelineStageFlagBits2::eBottomOfPipe);

    cmd.end();

    vk::raii::Fence fence(mCtx->device, vk::FenceCreateInfo{});
    vk::CommandBuffer cmdHandle = *cmd;
    mCtx->graphicsQueue.submit(
            vk::SubmitInfo{
                .commandBufferCount = 1,
                .pCommandBuffers = &cmdHandle,
            },
            *fence);

    std::ignore =
            mCtx->device.waitForFences(*fence, vk::True, std::numeric_limits<uint64_t>::max());

    auto* pixels = static_cast<uint8_t*>(readbackMemory.mapMemory(0, bufferSize));
    mCtx->device.invalidateMappedMemoryRanges(
            vk::MappedMemoryRange{ .memory = *readbackMemory, .offset = 0, .size = bufferSize });
    for (uint32_t i = 0; i < width * height; ++i) {
        std::swap(pixels[i * 4 + 0], pixels[i * 4 + 2]); // BGRA -> RGBA
        pixels[i * 4 + 3] = 255;
    }
    stbi_write_png(mScreenshotPath.c_str(), static_cast<int>(width), static_cast<int>(height), 4,
            pixels, static_cast<int>(width) * 4);
    readbackMemory.unmapMemory();
    std::cout << "Screenshot saved: " << mScreenshotPath << "\n";

    if (mShadowPipeline) {
        captureShadowMapDebug();
    }
    if (mAoPipeline) {
        captureAoTextureDebug();
        captureNormalsTextureDebug();
    }
    if (mExitAfterScreenshot) {
        glfwSetWindowShouldClose(mWindow, GLFW_TRUE);
    }
}

// Converts raw depth floats (sampler border = 1.0) to normalized grayscale RGBA bytes.
// Geometry depth is remapped to [0,1] within its own range and inverted (closer = brighter).
// Background pixels (depth == 1.0) map to black.
static std::vector<uint8_t> depthFloatsToGrayscaleRgba(float const* depthPixels,
        uint32_t pixelCount) {
    constexpr float kBorderDepth = 1.0f;

    float minGeometryDepth = kBorderDepth;
    float maxGeometryDepth = 0.0f;
    for (uint32_t i = 0; i < pixelCount; ++i) {
        float depth = depthPixels[i];
        if (depth < kBorderDepth) {
            minGeometryDepth = std::min(minGeometryDepth, depth);
            maxGeometryDepth = std::max(maxGeometryDepth, depth);
        }
    }
    float geometryDepthRange = maxGeometryDepth - minGeometryDepth;
    if (geometryDepthRange < 1e-5f) {
        geometryDepthRange = 1.0f;
    }

    std::vector<uint8_t> rgba(pixelCount * 4);
    for (uint32_t i = 0; i < pixelCount; ++i) {
        float depth = depthPixels[i];
        uint8_t grayscale;
        if (depth >= kBorderDepth) {
            grayscale = 0; // background → black
        } else {
            float normalizedDepth = (depth - minGeometryDepth) / geometryDepthRange;
            grayscale = static_cast<uint8_t>((1.0f - normalizedDepth) * 255.0f);
        }
        rgba[i * 4 + 0] = grayscale;
        rgba[i * 4 + 1] = grayscale;
        rgba[i * 4 + 2] = grayscale;
        rgba[i * 4 + 3] = 255;
    }
    return rgba;
}

void Renderer::captureImageToPng(vk::Image image, vk::ImageLayout currentLayout,
        vk::ImageAspectFlags aspect, uint32_t width, uint32_t height, uint32_t bytesPerPixel,
        std::string const& outputPath, PixelTransform transform) {
    uint32_t pixelCount = width * height;
    vk::DeviceSize readbackBufferSize = vk::DeviceSize(pixelCount) * bytesPerPixel;

    auto [readbackBuffer, readbackMemory] = vkutil::createBuffer(*mCtx, readbackBufferSize,
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);

    vk::raii::CommandBuffer cmd =
            std::move(mCtx->device
                              .allocateCommandBuffers(vk::CommandBufferAllocateInfo{
                                  .commandPool = *mCmds->commandPool,
                                  .level = vk::CommandBufferLevel::ePrimary,
                                  .commandBufferCount = 1,
                              })
                              .front());

    cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

    vkutil::transitionImageLayout(cmd, image, currentLayout, vk::ImageLayout::eTransferSrcOptimal,
            vk::AccessFlagBits2::eShaderRead, vk::AccessFlagBits2::eTransferRead,
            vk::PipelineStageFlagBits2::eFragmentShader, vk::PipelineStageFlagBits2::eTransfer,
            aspect);

    vk::BufferImageCopy copyRegion{
        .imageSubresource = { .aspectMask = aspect,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1 },
        .imageExtent = { width, height, 1 },
    };
    cmd.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, *readbackBuffer, copyRegion);

    vkutil::transitionImageLayout(cmd, image, vk::ImageLayout::eTransferSrcOptimal, currentLayout,
            vk::AccessFlagBits2::eTransferRead, vk::AccessFlagBits2::eShaderRead,
            vk::PipelineStageFlagBits2::eTransfer, vk::PipelineStageFlagBits2::eFragmentShader,
            aspect);

    cmd.end();

    vk::raii::Fence fence(mCtx->device, vk::FenceCreateInfo{});
    vk::CommandBuffer cmdHandle = *cmd;
    mCtx->graphicsQueue.submit(
            vk::SubmitInfo{ .commandBufferCount = 1, .pCommandBuffers = &cmdHandle }, *fence);
    if (mCtx->device.waitForFences(*fence, vk::True, std::numeric_limits<uint64_t>::max()) !=
            vk::Result::eSuccess) {
        throw std::runtime_error("captureImageToPng: fence wait failed");
    }

    void* mapped = readbackMemory.mapMemory(0, readbackBufferSize);
    mCtx->device.invalidateMappedMemoryRanges(vk::MappedMemoryRange{ .memory = *readbackMemory,
        .offset = 0,
        .size = readbackBufferSize });

    std::vector<uint8_t> rgba;
    if (transform) {
        rgba = transform(mapped, pixelCount);
    } else {
        rgba.assign(static_cast<uint8_t const*>(mapped),
                static_cast<uint8_t const*>(mapped) + pixelCount * 4);
    }
    readbackMemory.unmapMemory();

    stbi_write_png(outputPath.c_str(), static_cast<int>(width), static_cast<int>(height), 4,
            rgba.data(), static_cast<int>(width) * 4);
    std::cout << outputPath << " saved\n";
}

void Renderer::captureShadowMapDebug() {
    constexpr uint32_t shadowMapSize = SHADOW_MAP_SIZE;
    std::string path = screenshotOutputPath("shadow_depth.png");
    captureImageToPng(*mShadowPipeline->image, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eDepth, shadowMapSize, shadowMapSize, sizeof(float), path,
            [](void const* data, uint32_t pixelCount) {
                return depthFloatsToGrayscaleRgba(static_cast<float const*>(data), pixelCount);
            });
}

void Renderer::captureAoTextureDebug() {
    uint32_t width = mSwapchain->extent.width;
    uint32_t height = mSwapchain->extent.height;
    std::string path = screenshotOutputPath(mScene.gtao ? "gtao_texture.png" : "ao_texture.png");
    // Extract R (AO value) and expand to grayscale RGBA
    captureImageToPng(*mAoPipeline->aoRawImage, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor, width, height, 4, path,
            [](void const* data, uint32_t pixelCount) {
                auto* src = static_cast<uint8_t const*>(data);
                std::vector<uint8_t> rgba(pixelCount * 4);
                for (uint32_t i = 0; i < pixelCount; ++i) {
                    uint8_t ao = src[i * 4];
                    rgba[i * 4 + 0] = ao;
                    rgba[i * 4 + 1] = ao;
                    rgba[i * 4 + 2] = ao;
                    rgba[i * 4 + 3] = 255;
                }
                return rgba;
            });
}

void Renderer::captureNormalsTextureDebug() {
    uint32_t width = mSwapchain->extent.width;
    uint32_t height = mSwapchain->extent.height;
    std::string path = screenshotOutputPath("normals_texture.png");
    captureImageToPng(*mAoPipeline->normalsImage, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor, width, height, 4, path);
}

std::string Renderer::screenshotOutputPath(std::string const& filename) const {
    auto slash = mScreenshotPath.rfind('/');
    return (slash != std::string::npos ? mScreenshotPath.substr(0, slash + 1) : "") + filename;
}
