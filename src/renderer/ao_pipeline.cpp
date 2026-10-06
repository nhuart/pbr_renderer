#include "renderer/ao_pipeline.hpp"

#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "core/swapchain.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_inverse.hpp>

static vk::raii::Sampler makeNearestSampler(VulkanContext const& ctx, uint32_t mipLevels = 1) {
    return vk::raii::Sampler(ctx.device, vk::SamplerCreateInfo{
                                             .magFilter = vk::Filter::eNearest,
                                             .minFilter = vk::Filter::eNearest,
                                             .mipmapMode = vk::SamplerMipmapMode::eNearest,
                                             .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                                             .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                                             .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                                             .minLod = 0.0f,
                                             .maxLod = static_cast<float>(mipLevels - 1),
                                         });
}

AoPipeline::AoPipeline(VulkanContext const& ctx, Swapchain const& swapchain, SaoConfig const& cfg)
        : AoPipeline(ctx, swapchain, std::variant<SaoConfig, GtaoConfig>{ cfg }) {}

AoPipeline::AoPipeline(VulkanContext const& ctx, Swapchain const& swapchain, GtaoConfig const& cfg)
        : AoPipeline(ctx, swapchain, std::variant<SaoConfig, GtaoConfig>{ cfg }) {}

AoPipeline::AoPipeline(VulkanContext const& ctx, Swapchain const& swapchain,
        std::variant<SaoConfig, GtaoConfig> cfg)
        : config(std::move(cfg)) {
    createImages(ctx, swapchain.extent);
    createNormalsPass(ctx);
    if (depthMipLevelCount > 1) {
        createDepthMipPass(ctx);
    }
    createAoPass(ctx);
    createBlurPass(ctx);
}

void AoPipeline::createImages(VulkanContext const& ctx, vk::Extent2D extent) {
    constexpr vk::Format normalsFormat = vk::Format::eR8G8B8A8Unorm;
    // R=AO value (0=occluded, 1=unoccluded); G+B=linearized depth packed as 16-bit (G*256/257 +
    // B/257) for bilateral blur: depth is compared per-neighbor to avoid blurring AO across depth
    // discontinuities
    constexpr vk::Format aoFormat = vk::Format::eR8G8B8A8Unorm;
    vk::Format depthFormat = vkutil::findDepthFormat(ctx);

    auto createImage = [&](vk::Format format, vk::ImageUsageFlags usage)
            -> std::pair<vk::raii::Image, vk::raii::DeviceMemory> {
        return vkutil::createImage(ctx, extent.width, extent.height, 1, vk::SampleCountFlagBits::e1,
                format, vk::ImageTiling::eOptimal, usage, vk::MemoryPropertyFlagBits::eDeviceLocal);
    };

    constexpr vk::ImageUsageFlags colorUsage = vk::ImageUsageFlagBits::eColorAttachment |
                                               vk::ImageUsageFlagBits::eSampled |
                                               vk::ImageUsageFlagBits::eTransferSrc;

    // Match Filament's min(8, maxLevelCount(width, height) - 5), with dimensions
    // clamped to at least 32 pixels as in its structure pass.
    if (std::holds_alternative<SaoConfig>(config)) {
        uint32_t largestDimension = std::max({ 32u, extent.width, extent.height });
        depthMipLevelCount = std::min(8, std::bit_width(largestDimension) - 5);
    }

    auto [di, dm] = vkutil::createImage(ctx, extent.width, extent.height, depthMipLevelCount,
            vk::SampleCountFlagBits::e1, depthFormat, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
    depthImage = std::move(di);
    depthMemory = std::move(dm);
    depthView = vkutil::createImageView(ctx, *depthImage, depthFormat,
            vk::ImageAspectFlagBits::eDepth, depthMipLevelCount);
    depthMipViews.reserve(depthMipLevelCount);
    for (uint32_t level = 0; level < depthMipLevelCount; ++level) {
        depthMipViews.emplace_back(ctx.device,
                vk::ImageViewCreateInfo{
                    .image = *depthImage,
                    .viewType = vk::ImageViewType::e2D,
                    .format = depthFormat,
                    .subresourceRange = { vk::ImageAspectFlagBits::eDepth, level, 1, 0, 1 },
                });
    }
    depthSampler = makeNearestSampler(ctx, depthMipLevelCount);

    auto [ni, nm] = createImage(normalsFormat, colorUsage);
    normalsImage = std::move(ni);
    normalsMemory = std::move(nm);
    normalsView = vkutil::createImageView(ctx, *normalsImage, normalsFormat);
    normalsSampler = makeNearestSampler(ctx);

    auto [ri, rm] = createImage(aoFormat, colorUsage);
    aoRawImage = std::move(ri);
    aoRawMemory = std::move(rm);
    aoRawView = vkutil::createImageView(ctx, *aoRawImage, aoFormat);
    aoRawSampler = makeNearestSampler(ctx);

    auto [bi, bm] = createImage(aoFormat, colorUsage);
    aoBlurImage = std::move(bi);
    aoBlurMemory = std::move(bm);
    aoBlurView = vkutil::createImageView(ctx, *aoBlurImage, aoFormat);
    aoBlurSampler = makeNearestSampler(ctx);
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
            .maxLevel = static_cast<int>(depthMipLevelCount - 1),
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
