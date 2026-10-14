#include "renderer/ao_pipeline.hpp"

#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "core/swapchain.hpp"
#include "renderer/geometry_prepass.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_inverse.hpp>

AoPipeline::AoPipeline(VulkanContext const& ctx, Swapchain const& swapchain,
        GeometryPrepass const& geometryPrepass, SaoConfig const& cfg)
        : AoPipeline(ctx, swapchain, geometryPrepass, std::variant<SaoConfig, GtaoConfig>{ cfg }) {}

AoPipeline::AoPipeline(VulkanContext const& ctx, Swapchain const& swapchain,
        GeometryPrepass const& geometryPrepass, GtaoConfig const& cfg)
        : AoPipeline(ctx, swapchain, geometryPrepass, std::variant<SaoConfig, GtaoConfig>{ cfg }) {}

AoPipeline::AoPipeline(VulkanContext const& ctx, Swapchain const& swapchain,
        GeometryPrepass const& geometryPrepass, std::variant<SaoConfig, GtaoConfig> cfg)
        : config(std::move(cfg)),
          mGeometryPrepass(geometryPrepass) {
    createImages(ctx, swapchain.extent);
    if (std::holds_alternative<SaoConfig>(config) && mGeometryPrepass.depthMipLevelCount > 1) {
        createDepthMipPass(ctx);
    }
    createAoPass(ctx);
    createBlurPass(ctx);
}

uint32_t AoPipeline::saoDepthMipLevels(vk::Extent2D extent) {
    // Match Filament's depth pyramid dimensions, clamped to at least 32 pixels.
    uint32_t largestDimension = std::max({ 32u, extent.width, extent.height });
    return std::min(8, std::bit_width(largestDimension) - 5);
}

void AoPipeline::createImages(VulkanContext const& ctx, vk::Extent2D extent) {
    // R=AO value (0=occluded, 1=unoccluded); G+B=linearized depth packed as 16-bit (G*256/257 +
    // B/257) for bilateral blur: depth is compared per-neighbor to avoid blurring AO across depth
    // discontinuities
    constexpr vk::Format aoFormat = vk::Format::eR8G8B8A8Unorm;

    auto createImage = [&](vk::Format format, vk::ImageUsageFlags usage)
            -> std::pair<vk::raii::Image, vk::raii::DeviceMemory> {
        return vkutil::createImage(ctx, extent.width, extent.height, 1, vk::SampleCountFlagBits::e1,
                format, vk::ImageTiling::eOptimal, usage, vk::MemoryPropertyFlagBits::eDeviceLocal);
    };

    constexpr vk::ImageUsageFlags colorUsage = vk::ImageUsageFlagBits::eColorAttachment |
                                               vk::ImageUsageFlagBits::eSampled |
                                               vk::ImageUsageFlagBits::eTransferSrc;

    auto [ri, rm] = createImage(aoFormat, colorUsage);
    aoRawImage = std::move(ri);
    aoRawMemory = std::move(rm);
    aoRawView = vkutil::createImageView(ctx, *aoRawImage, aoFormat);
    aoRawSampler = vkutil::createNearestSampler(ctx);

    auto [bi, bm] = createImage(aoFormat, colorUsage);
    aoBlurImage = std::move(bi);
    aoBlurMemory = std::move(bm);
    aoBlurView = vkutil::createImageView(ctx, *aoBlurImage, aoFormat);
    aoBlurSampler = vkutil::createNearestSampler(ctx);
}

void AoPipeline::updateUBOs(uint32_t frameIndex, glm::mat4 const& view, glm::mat4 const& proj,
        float fovYRad, float height, float nearPlane, float farPlane) {
    float projScale = (0.5f * height) / std::tan(0.5f * fovYRad);

    if (auto const* gtao = std::get_if<GtaoConfig>(&config)) {
        GtaoUBO ubo{
            .proj = proj,
            .invProj = glm::inverse(proj),
            .radius = gtao->radius,
            .thicknessHeuristic = gtao->thicknessHeuristic,
            .power = gtao->power,
            .intensity = gtao->intensity,
            .projScale = projScale,
            .stepCount = gtao->stepCount,
            .directionCount = gtao->directionCount,
            .nearPlane = nearPlane,
            .farPlane = farPlane,
        };
        memcpy(aoUboMapped[frameIndex], &ubo, sizeof(ubo));
    } else {
        auto const& sao = std::get<SaoConfig>(config);
        SaoUBO ubo{
            .proj = proj,
            .invProj = glm::inverse(proj),
            .radius = sao.radius,
            .bias = sao.bias,
            .power = sao.power,
            .intensity = sao.intensity,
            .projScale = projScale,
            .sampleCount = sao.sampleCount,
            .spiralTurns = sao.spiralTurns,
            .nearPlane = nearPlane,
            .farPlane = farPlane,
            .maxLevel = static_cast<int>(mGeometryPrepass.depthMipLevelCount - 1),
        };
        memcpy(aoUboMapped[frameIndex], &ubo, sizeof(ubo));
    }

    // farPlaneOverEdgeDistance = -far / bilateralThreshold (matching Filament's convention)
    float depthThreshold = std::visit([](auto const& cfg) { return cfg.depthThreshold; }, config);
    int kernelRadius = std::visit([](auto const& cfg) { return cfg.kernelRadius; }, config);
    int sampleStride = std::holds_alternative<SaoConfig>(config) ? 2 : 1;
    float farPlaneOverEdge = -farPlane / depthThreshold;
    for (uint32_t pi = 0; pi < 2; pi++) {
        BlurUBO blurUbo{
            .passIndex = static_cast<int>(pi),
            .farPlaneOverEdgeDistance = farPlaneOverEdge,
            .kernelRadius = kernelRadius,
            .sampleStride = sampleStride,
        };
        memcpy(blurUboMapped[frameIndex][pi], &blurUbo, sizeof(blurUbo));
    }
}
