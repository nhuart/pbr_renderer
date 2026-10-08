#pragma once

#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

struct VulkanContext;

struct Swapchain {
    static constexpr vk::Format HDR_COLOR_FORMAT = vk::Format::eR16G16B16A16Sfloat;

    vk::raii::SwapchainKHR swapChain = nullptr;
    std::vector<vk::Image> images;
    vk::SurfaceFormatKHR surfaceFormat;
    vk::Extent2D extent;
    std::vector<vk::raii::ImageView> imageViews;
    vk::raii::Image colorImage = nullptr;
    vk::raii::DeviceMemory colorImageMemory = nullptr;
    vk::raii::ImageView colorImageView = nullptr;
    vk::raii::Image hdrImage = nullptr;
    vk::raii::DeviceMemory hdrImageMemory = nullptr;
    vk::raii::ImageView hdrImageView = nullptr;
    vk::raii::Sampler hdrSampler = nullptr;
    vk::raii::Image depthImage = nullptr;
    vk::raii::DeviceMemory depthImageMemory = nullptr;
    vk::raii::ImageView depthImageView = nullptr;

    Swapchain(VulkanContext const& ctx, GLFWwindow* window);
    void recreate(VulkanContext const& ctx, GLFWwindow* window);
    void cleanup();

private:
    void create(VulkanContext const& ctx, GLFWwindow* window);
    void createColorResources(VulkanContext const& ctx);
    void createHdrResources(VulkanContext const& ctx);
    void createDepthResources(VulkanContext const& ctx);
    static vk::SurfaceFormatKHR chooseFormat(std::vector<vk::SurfaceFormatKHR> const&);
    static vk::PresentModeKHR choosePresentMode(std::vector<vk::PresentModeKHR> const&);
    static vk::Extent2D chooseExtent(vk::SurfaceCapabilitiesKHR const&, GLFWwindow*);
    static uint32_t chooseMinImageCount(vk::SurfaceCapabilitiesKHR const&);
};
