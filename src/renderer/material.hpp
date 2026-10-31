#pragma once

#include <compare>
#include <string>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "renderer/gpu_types.hpp"

struct VulkanContext;
struct TextureAtlas;
struct IblEnvironment;
struct ShadowPipeline;
struct AoPipeline;

struct MaterialPipelineKey {
    std::string vertexShader;
    std::string fragmentShader;
    ShaderFeatures features = ShaderFeatures::None;
    bool doubleSided = false;

    auto operator<=>(MaterialPipelineKey const&) const = default;
};

struct MaterialResources {
    TextureAtlas const& texture;
    vk::raii::Buffer const& lightBuffer;
    IblEnvironment const* ibl = nullptr;
    TextureAtlas const* brdfLut = nullptr;
    TextureAtlas const* normalMap = nullptr;
    ShadowPipeline const* shadowMap = nullptr;
    AoPipeline const* aoPipeline = nullptr;
};

struct MaterialInstance {
    std::vector<vk::raii::Buffer> uniformBuffers;
    std::vector<vk::raii::DeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped;
    vk::raii::Buffer shBuffer{ nullptr };
    vk::raii::DeviceMemory shBufferMemory{ nullptr };
    vk::raii::DescriptorSets descriptorSets{ nullptr };

    MaterialInstance() = default;
    MaterialInstance(MaterialInstance&&) = default;
    MaterialInstance& operator=(MaterialInstance&&) = default;
    MaterialInstance(MaterialInstance const&) = delete;
    MaterialInstance& operator=(MaterialInstance const&) = delete;

    void updateUBO(uint32_t frameIndex, UniformBufferObject const& ubo);
};

struct Material {
    vk::raii::DescriptorSetLayout descriptorSetLayout{ nullptr };
    vk::raii::PipelineLayout pipelineLayout{ nullptr };
    vk::raii::DescriptorPool descriptorPool{ nullptr };
    vk::raii::Pipeline pipeline{ nullptr };

    Material(VulkanContext const& ctx, MaterialPipelineKey const& key, uint32_t instanceCount);

    [[nodiscard]] MaterialInstance createInstance(VulkanContext const& ctx,
            MaterialResources const& resources) const;
};
