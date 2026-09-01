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

struct MeshPipeline {
    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::raii::PipelineLayout      pipelineLayout      = nullptr;
    vk::raii::DescriptorPool      descriptorPool      = nullptr;
    vk::raii::Pipeline            pipeline            = nullptr;

    MeshPipeline(VulkanContext const& ctx, Swapchain const& swapchain);
    void allocateDescriptorSets(VulkanContext const& ctx, std::vector<GameObject>& objects,
            vk::Sampler sampler, vk::ImageView imageView);

private:
    static std::vector<char> readFile(std::string const& path);
    [[nodiscard]] vk::raii::ShaderModule createShaderModule(VulkanContext const& ctx,
            std::vector<char> const& code) const;
};
