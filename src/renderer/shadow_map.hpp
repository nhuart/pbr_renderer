#pragma once

#include <vector>

#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>

#include "scene/types.hpp"

struct VulkanContext;
struct CommandService;

constexpr uint32_t SHADOW_MAP_SIZE = 2048;

// Per-object, per-frame shadow UBO buffers and descriptor sets
struct ShadowObjectData {
    std::vector<vk::raii::Buffer> uboBuffers;
    std::vector<vk::raii::DeviceMemory> uboMemory;
    std::vector<void*> uboMapped;
    vk::raii::DescriptorSets descriptorSets{ nullptr };
};

struct ShadowMap {
    vk::raii::Image image{ nullptr };
    vk::raii::DeviceMemory memory{ nullptr };
    vk::raii::ImageView imageView{ nullptr };
    vk::raii::Sampler sampler{ nullptr };

    // Depth-only pipeline
    vk::raii::DescriptorSetLayout descriptorSetLayout{ nullptr };
    vk::raii::PipelineLayout pipelineLayout{ nullptr };
    vk::raii::DescriptorPool descriptorPool{ nullptr };
    vk::raii::Pipeline pipeline{ nullptr };

    // Per-object data (one entry per render object that castShadows)
    std::vector<ShadowObjectData> objects;

    // Shared light-space matrix (VP only, no model)
    glm::mat4 lightSpaceMatrix{ 1.0f };

    // Per-frame UBOs for material binding 8 (lightSpaceMatrix, no model — used in fragment shader)
    std::vector<vk::raii::Buffer> shadowUboBuffers;
    std::vector<vk::raii::DeviceMemory> shadowUboMemory;
    std::vector<void*> shadowUboMapped;

    ShadowMap(VulkanContext const& ctx, CommandService const& cmds, uint32_t objectCount);

    void allocateObjectData(VulkanContext const& ctx, uint32_t objectCount);
    void updateLightSpaceMatrix(DirectionalLight const& light);
    void updateObjectUBO(uint32_t objectIndex, uint32_t frameIndex, glm::mat4 const& model);
    void updateFragmentUBO(uint32_t frameIndex);
};
