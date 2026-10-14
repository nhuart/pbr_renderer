#pragma once

#include <array>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "core/config.hpp"

struct VulkanContext;
struct Swapchain;

// Matches standard.vert's uniform layout for the view-space normals fragment shader.
struct NormalsUBO {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::mat4 normalMatrix;
    alignas(16) glm::vec4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    alignas(16) glm::vec4 cameraPos = {};
    alignas(16) glm::vec4 pbrParams = {};
};

struct GeometryPrepass {
    // The prepass writes mip 0; consumers may generate lower-resolution depth mips.
    uint32_t depthMipLevelCount;
    vk::raii::Image depthImage{ nullptr };
    vk::raii::DeviceMemory depthMemory{ nullptr };
    vk::raii::ImageView depthView{ nullptr };
    std::vector<vk::raii::ImageView> depthMipViews;
    vk::raii::Sampler depthSampler{ nullptr };

    vk::raii::Image normalsImage{ nullptr };
    vk::raii::DeviceMemory normalsMemory{ nullptr };
    vk::raii::ImageView normalsView{ nullptr };
    vk::raii::Sampler normalsSampler{ nullptr };

    vk::raii::DescriptorSetLayout normalsDescLayout{ nullptr };
    vk::raii::PipelineLayout normalsPipeLayout{ nullptr };
    vk::raii::DescriptorPool normalsDescPool{ nullptr };
    vk::raii::Pipeline normalsPipeline{ nullptr };

    struct NormalsObject {
        std::array<vk::raii::Buffer, MAX_FRAMES_IN_FLIGHT> uboBuffers{ nullptr, nullptr };
        std::array<vk::raii::DeviceMemory, MAX_FRAMES_IN_FLIGHT> uboMemory{ nullptr, nullptr };
        std::array<void*, MAX_FRAMES_IN_FLIGHT> uboMapped{};
        vk::raii::DescriptorSets descriptorSets{ nullptr };
    };
    std::vector<NormalsObject> normalsObjects;

    GeometryPrepass(VulkanContext const& ctx, Swapchain const& swapchain,
            uint32_t depthMipLevels = 1);
    void allocateObjects(VulkanContext const& ctx, uint32_t objectCount);
    void updateObjectUBO(uint32_t objectIndex, uint32_t frameIndex, NormalsUBO const& ubo);

private:
    void createImages(VulkanContext const& ctx, vk::Extent2D extent);
    void createNormalsPass(VulkanContext const& ctx);
};
