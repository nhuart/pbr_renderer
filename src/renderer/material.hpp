#pragma once

#include <string>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "scene/types.hpp"

struct VulkanContext;
struct Swapchain;
struct TextureAtlas;

struct MaterialInstance {
    std::vector<vk::raii::Buffer> uniformBuffers;
    std::vector<vk::raii::DeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped;
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

    Material(VulkanContext const& ctx, Swapchain const& swapchain,
            std::string const& vertexShaderFilename, std::string const& fragmentShaderFilename,
            bool doubleSided = false);

    [[nodiscard]] MaterialInstance createInstance(VulkanContext const& ctx,
            TextureAtlas const& texture,
            std::vector<vk::raii::Buffer> const& lightBuffers) const;

private:
    static std::vector<char> readFile(std::string const& path);
    [[nodiscard]] vk::raii::ShaderModule createShaderModule(VulkanContext const& ctx,
            std::vector<char> const& code) const;
};
