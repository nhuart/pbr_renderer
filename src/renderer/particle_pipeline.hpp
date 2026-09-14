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
struct CommandService;

struct ParticlePipeline {
    vk::raii::Pipeline particlePipeline = nullptr;
    vk::raii::PipelineLayout particlePipelineLayout = nullptr;
    vk::raii::DescriptorSetLayout computeDescriptorSetLayout = nullptr;
    vk::raii::PipelineLayout computePipelineLayout = nullptr;
    vk::raii::Pipeline computePipeline = nullptr;
    vk::raii::DescriptorPool computeDescriptorPool = nullptr;
    std::vector<vk::raii::DescriptorSet> computeDescriptorSets;
    std::vector<vk::raii::Buffer> shaderStorageBuffers;
    std::vector<vk::raii::DeviceMemory> shaderStorageBuffersMemory;
    std::vector<vk::raii::Buffer> computeUniformBuffers;
    std::vector<vk::raii::DeviceMemory> computeUniformBuffersMemory;
    std::vector<void*> computeUniformBuffersMapped;

    ParticlePipeline(VulkanContext const& ctx, Swapchain const& swapchain,
            CommandService const& cmds, ParticleSystem const& particleSystem);
};
