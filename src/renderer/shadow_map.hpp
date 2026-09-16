#pragma once

#include <array>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "scene/types.hpp"

struct VulkanContext;
struct CommandService;

constexpr uint32_t SHADOW_MAP_SIZE = 2048;

struct ShadowFrameBuffer {
    vk::raii::Buffer buffer{ nullptr };
    vk::raii::DeviceMemory memory{ nullptr };
    void* mapped = nullptr;
};

// Per-object, per-frame shadow UBO buffers and descriptor sets
struct ShadowObjectData {
    std::array<ShadowFrameBuffer, MAX_FRAMES_IN_FLIGHT> frames;
    vk::raii::DescriptorSets descriptorSets{ nullptr };
};

struct ShadowMap {
    vk::raii::Image image{ nullptr };
    vk::raii::DeviceMemory memory{ nullptr };
    vk::raii::ImageView imageView{ nullptr };
    vk::raii::Sampler sampler{ nullptr };

    vk::raii::DescriptorSetLayout descriptorSetLayout{ nullptr };
    vk::raii::PipelineLayout pipelineLayout{ nullptr };
    vk::raii::DescriptorPool descriptorPool{ nullptr };
    vk::raii::Pipeline pipeline{ nullptr };

    // Per-object data (one entry per render object that castShadows)
    std::vector<ShadowObjectData> objects;

    // Shared light-space matrix (VP only, no model)
    glm::mat4 lightSpaceMatrix{ 1.0f };
    ShadowType shadowType = ShadowType::Hard;
    float shadowBias = 0.005f;

    // Per-frame UBOs for material binding 8 (lightSpaceTransform VP — used in fragment shader)
    std::array<ShadowFrameBuffer, MAX_FRAMES_IN_FLIGHT> fragmentUbo;

    ShadowMap(VulkanContext const& ctx, CommandService const& cmds, uint32_t objectCount);

    void allocateObjectData(VulkanContext const& ctx, uint32_t objectCount);
    void updateLightSpaceMatrix(DirectionalLight const& light);
    void updateObjectUBO(uint32_t objectIndex, uint32_t frameIndex, glm::mat4 const& model);
    void updateFragmentUBO(uint32_t frameIndex);

private:
    void createDepthImage(VulkanContext const& ctx, CommandService const& cmds);
    void createFragmentUboBuffers(VulkanContext const& ctx);
    void createPipeline(VulkanContext const& ctx, uint32_t objectCount);
};
