#pragma once

#include <vulkan/vulkan_raii.hpp>

struct VulkanContext;
struct Swapchain;
enum class ToneMapping;

class TonemapPipeline {
public:
    TonemapPipeline(VulkanContext const& ctx, Swapchain const& swapchain, ToneMapping mode);
    void draw(vk::raii::CommandBuffer const& cmd) const;

private:
    vk::raii::DescriptorSetLayout descriptorLayout{ nullptr };
    vk::raii::DescriptorPool descriptorPool{ nullptr };
    vk::raii::DescriptorSets descriptorSets{ nullptr };
    vk::raii::PipelineLayout pipelineLayout{ nullptr };
    vk::raii::Pipeline pipeline{ nullptr };

    void createDescriptorResources(VulkanContext const& ctx, Swapchain const& swapchain);
    void createGraphicsPipeline(VulkanContext const& ctx, Swapchain const& swapchain,
            ToneMapping mode);
};
