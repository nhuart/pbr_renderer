#pragma once

#include <array>
#include <glm/glm.hpp>
#include <variant>
#include <vulkan/vulkan_raii.hpp>

#include "core/config.hpp"
#include "scene/types.hpp"

struct VulkanContext;
struct Swapchain;

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

// NormalsUBO mirrors UniformBufferObject so view_space_normals.frag works with standard.vert
struct NormalsUBO {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::mat4 normalMatrix;
    alignas(16) glm::vec4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    alignas(16) glm::vec4 cameraPos = {};
    alignas(16) glm::vec4 pbrParams = {};
};

struct AoPipeline {
    // Single-sample depth image shared by normals prepass and AO pass
    uint32_t depthMipLevelCount = 1;
    vk::raii::Image depthImage{ nullptr };
    vk::raii::DeviceMemory depthMemory{ nullptr };
    // Full-range view for AO sampling; per-mip views for the prepass and downsampling.
    vk::raii::ImageView depthView{ nullptr };
    std::vector<vk::raii::ImageView> depthMipViews; // individual mips for rendering
    vk::raii::Sampler depthSampler{ nullptr };

    // SAO depth pyramid: rotated-grid sub-sampling of the preceding mip.
    vk::raii::DescriptorSetLayout depthMipDescLayout{ nullptr };
    vk::raii::PipelineLayout depthMipPipeLayout{ nullptr };
    vk::raii::DescriptorPool depthMipDescPool{ nullptr };
    vk::raii::Pipeline depthMipPipeline{ nullptr };
    std::vector<vk::raii::DescriptorSet> depthMipDescSets;

    // Normals prepass (writes view-space normals)
    vk::raii::Image normalsImage{ nullptr };
    vk::raii::DeviceMemory normalsMemory{ nullptr };
    vk::raii::ImageView normalsView{ nullptr };
    vk::raii::Sampler normalsSampler{ nullptr };

    vk::raii::DescriptorSetLayout normalsDescLayout{ nullptr };
    vk::raii::PipelineLayout normalsPipeLayout{ nullptr };
    vk::raii::DescriptorPool normalsDescPool{ nullptr };
    vk::raii::Pipeline normalsPipeline{ nullptr };

    // Per-object, per-frame normals UBOs and descriptor sets
    struct NormalsObject {
        std::array<vk::raii::Buffer, MAX_FRAMES_IN_FLIGHT> uboBuffers{ nullptr, nullptr };
        std::array<vk::raii::DeviceMemory, MAX_FRAMES_IN_FLIGHT> uboMemory{ nullptr, nullptr };
        std::array<void*, MAX_FRAMES_IN_FLIGHT> uboMapped{};
        vk::raii::DescriptorSets descriptorSets{ nullptr };
    };
    std::vector<NormalsObject> normalsObjects;

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

    AoPipeline(VulkanContext const& ctx, Swapchain const& swapchain, SaoConfig const& cfg);
    AoPipeline(VulkanContext const& ctx, Swapchain const& swapchain, GtaoConfig const& cfg);

    void allocateNormalsObjects(VulkanContext const& ctx, uint32_t objectCount);
    void updateNormalsUBO(uint32_t objectIndex, uint32_t frameIndex, NormalsUBO const& ubo);
    void updateUBOs(uint32_t frameIndex, glm::mat4 const& view, glm::mat4 const& proj,
            float fovYRad, float height, float nearPlane, float farPlane);

    [[nodiscard]] vk::ImageView finalAoView() const { return *aoRawView; }
    [[nodiscard]] vk::Sampler finalAoSampler() const { return *aoRawSampler; }

private:
    AoPipeline(VulkanContext const& ctx, Swapchain const& swapchain,
            std::variant<SaoConfig, GtaoConfig> cfg);
    void createImages(VulkanContext const& ctx, vk::Extent2D extent);
    void createNormalsPass(VulkanContext const& ctx);
    void createDepthMipPass(VulkanContext const& ctx);
    void createAoPass(VulkanContext const& ctx);
    void createBlurPass(VulkanContext const& ctx);
};
