#pragma once

#include <array>

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "core/config.hpp"

struct VulkanContext;
struct AoPipeline;
struct ShadowPipeline;

struct VolumetricUBO {
    alignas(16) glm::mat4 invViewProj;
    alignas(16) glm::mat4 lightSpaceMatrix;
    alignas(16) glm::vec4 cameraPos;
    alignas(16) glm::vec4 lightDirection; // xyz = toward light, w = shadow bias
    alignas(16) glm::vec4 lightColor;
    alignas(16) float extinctionCoefficient;
    float scatteringAlbedo;
    float maxDistance;
    float anisotropy;
    alignas(16) glm::vec4 boundsMin; // w = 1 if the participating medium is bounded
    alignas(16) glm::vec4 boundsMax;
    alignas(16) int stepCount;
};

struct VolumetricPipeline {
    VolumetricPipeline(VulkanContext const& ctx, AoPipeline const& depth,
            ShadowPipeline const& shadow);
    void update(uint32_t frame, VolumetricUBO const& ubo);
    void draw(vk::raii::CommandBuffer const& cmd, uint32_t frame) const;

private:
    vk::raii::DescriptorSetLayout descriptorLayout{ nullptr };
    vk::raii::DescriptorPool descriptorPool{ nullptr };
    vk::raii::PipelineLayout pipelineLayout{ nullptr };
    vk::raii::Pipeline pipeline{ nullptr };
    std::array<vk::raii::Buffer, MAX_FRAMES_IN_FLIGHT> buffers{ nullptr, nullptr };
    std::array<vk::raii::DeviceMemory, MAX_FRAMES_IN_FLIGHT> memories{ nullptr, nullptr };
    std::array<void*, MAX_FRAMES_IN_FLIGHT> mapped{};
    vk::raii::DescriptorSets descriptorSets{ nullptr };

    void createDescriptorResources(VulkanContext const& ctx, AoPipeline const& depth,
            ShadowPipeline const& shadow);
    void createDescriptorLayout(VulkanContext const& ctx);
    void allocateDescriptorSets(VulkanContext const& ctx);
    void initializeFrameDescriptors(VulkanContext const& ctx, AoPipeline const& depth,
            ShadowPipeline const& shadow);
    void createGraphicsPipeline(VulkanContext const& ctx);
};
