#pragma once

#include <array>
#include <glm/glm.hpp>
#include <variant>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

#include "core/config.hpp"
#include "scene/types.hpp"

struct VulkanContext;
struct Swapchain;
struct GeometryPrepass;

struct SaoUBO {
    alignas(16) glm::mat4 proj;
    alignas(16) glm::mat4 invProj;
    float radius;
    float bias;
    float power;
    float intensity;
    float projScale;
    int sampleCount;
    int spiralTurns;
    float nearPlane;
    float farPlane;
    int maxLevel;
    float pad;
};

// Shares the descriptor layout with SaoUBO, but uses horizon-search parameters.
struct GtaoUBO {
    alignas(16) glm::mat4 proj;
    alignas(16) glm::mat4 invProj;
    float radius;
    float thicknessHeuristic;
    float power;
    float intensity;
    float projScale;
    int stepCount;
    int directionCount;
    float nearPlane;
    float farPlane;
    float pad[2];
};
static_assert(sizeof(GtaoUBO) == sizeof(SaoUBO));

struct BlurUBO {
    int passIndex;
    float farPlaneOverEdgeDistance; // -far / bilateralThreshold
    int kernelRadius;
    int sampleStride;
};

struct AoPipeline {
    // SAO depth pyramid: rotated-grid sub-sampling of the preceding mip.
    vk::raii::DescriptorSetLayout depthMipDescLayout{ nullptr };
    vk::raii::PipelineLayout depthMipPipeLayout{ nullptr };
    vk::raii::DescriptorPool depthMipDescPool{ nullptr };
    vk::raii::Pipeline depthMipPipeline{ nullptr };
    std::vector<vk::raii::DescriptorSet> depthMipDescSets;

    // Raw AO image (output of the selected occlusion pass)
    vk::raii::Image aoRawImage{ nullptr };
    vk::raii::DeviceMemory aoRawMemory{ nullptr };
    vk::raii::ImageView aoRawView{ nullptr };
    vk::raii::Sampler aoRawSampler{ nullptr };

    // Blurred AO image (output of bilateral blur)
    vk::raii::Image aoBlurImage{ nullptr };
    vk::raii::DeviceMemory aoBlurMemory{ nullptr };
    vk::raii::ImageView aoBlurView{ nullptr };
    vk::raii::Sampler aoBlurSampler{ nullptr };

    // SAO or GTAO occlusion pipeline (shared descriptor layout and render targets)
    vk::raii::DescriptorSetLayout aoDescLayout{ nullptr };
    vk::raii::PipelineLayout aoPipeLayout{ nullptr };
    vk::raii::DescriptorPool aoDescPool{ nullptr };
    vk::raii::Pipeline aoPipeline{ nullptr };
    std::vector<vk::raii::DescriptorSet> aoDescSets;
    std::vector<vk::raii::Buffer> aoUboBuffers;
    std::vector<vk::raii::DeviceMemory> aoUboMemory;
    std::vector<void*> aoUboMapped;

    // Blur pipeline (shared for H and V passes, different UBO)
    vk::raii::DescriptorSetLayout blurDescLayout{ nullptr };
    vk::raii::PipelineLayout blurPipeLayout{ nullptr };
    vk::raii::DescriptorPool blurDescPool{ nullptr };
    vk::raii::Pipeline blurPipeline{ nullptr };
    // [frame][pass]: pass 0=H writes aoBlur, pass 1=V reads aoBlur writes aoRaw (ping-pong)
    std::vector<std::array<vk::raii::DescriptorSet, 2>> blurDescSets;
    std::vector<std::array<vk::raii::Buffer, 2>> blurUboBuffers;
    std::vector<std::array<vk::raii::DeviceMemory, 2>> blurUboMemory;
    std::vector<std::array<void*, 2>> blurUboMapped;

    std::variant<SaoConfig, GtaoConfig> config;
    AoPipeline(VulkanContext const& ctx, Swapchain const& swapchain,
            GeometryPrepass const& geometryPrepass, SaoConfig const& cfg);
    AoPipeline(VulkanContext const& ctx, Swapchain const& swapchain,
            GeometryPrepass const& geometryPrepass, GtaoConfig const& cfg);

    [[nodiscard]] static uint32_t saoDepthMipLevels(vk::Extent2D extent);
    void updateUBOs(uint32_t frameIndex, glm::mat4 const& view, glm::mat4 const& proj,
            float fovYRad, float height, float nearPlane, float farPlane);

    [[nodiscard]] vk::ImageView finalAoView() const { return *aoRawView; }
    [[nodiscard]] vk::Sampler finalAoSampler() const { return *aoRawSampler; }

private:
    GeometryPrepass const& mGeometryPrepass;
    AoPipeline(VulkanContext const& ctx, Swapchain const& swapchain,
            GeometryPrepass const& geometryPrepass, std::variant<SaoConfig, GtaoConfig> cfg);
    void createImages(VulkanContext const& ctx, vk::Extent2D extent);
    void createDepthMipPass(VulkanContext const& ctx);
    void createAoPass(VulkanContext const& ctx);
    void createBlurPass(VulkanContext const& ctx);
};
