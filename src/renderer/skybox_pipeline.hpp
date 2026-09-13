#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <glm/glm.hpp>

struct VulkanContext;
struct Swapchain;
struct IblEnvironment;

struct SkyboxUBO {
    glm::mat4 invProj;
    glm::mat4 invView;
};

struct SkyboxPipeline {
    vk::raii::DescriptorSetLayout descriptorSetLayout{ nullptr };
    vk::raii::PipelineLayout pipelineLayout{ nullptr };
    vk::raii::DescriptorPool descriptorPool{ nullptr };
    vk::raii::Pipeline pipeline{ nullptr };
    std::vector<vk::raii::DescriptorSet> descriptorSets;
    std::vector<vk::raii::Buffer> uniformBuffers;
    std::vector<vk::raii::DeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped;

    SkyboxPipeline(VulkanContext const& ctx, Swapchain const& swapchain, IblEnvironment const& ibl);

    void updateUBO(uint32_t frameIndex, SkyboxUBO const& ubo);

private:
    static std::vector<char> readFile(std::string const& path);
    [[nodiscard]] vk::raii::ShaderModule createShaderModule(VulkanContext const& ctx,
            std::vector<char> const& code) const;
};
