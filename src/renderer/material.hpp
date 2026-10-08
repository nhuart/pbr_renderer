#pragma once

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

    Material(VulkanContext const& ctx, std::string const& vertexShaderFilename,
            std::string const& fragmentShaderFilename,
            ShaderFeatures features = ShaderFeatures::None, bool doubleSided = false);

    [[nodiscard]] MaterialInstance createInstance(VulkanContext const& ctx,
            TextureAtlas const& texture, vk::raii::Buffer const& lightBuffer,
            IblEnvironment const* ibl = nullptr, TextureAtlas const* normalMap = nullptr,
            ShadowPipeline const* shadowMap = nullptr,
            AoPipeline const* aoPipeline = nullptr) const;
};
