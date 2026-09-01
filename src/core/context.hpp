#pragma once

#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

inline const std::vector<char const*> VALIDATION_LAYERS = {"VK_LAYER_KHRONOS_validation"};

#ifdef NDEBUG
constexpr bool ENABLE_VALIDATION_LAYERS = false;
#else
constexpr bool ENABLE_VALIDATION_LAYERS = true;
#endif

struct VulkanContext {
    vk::raii::Context                context;
    vk::raii::Instance               instance       = nullptr;
    vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
    vk::raii::PhysicalDevice         physicalDevice = nullptr;
    vk::raii::Device                 device         = nullptr;
    vk::raii::Queue                  graphicsQueue  = nullptr;
    vk::raii::Queue                  computeQueue   = nullptr;
    uint32_t                         graphicsQueueFamilyIndex = 0;
    vk::raii::SurfaceKHR             surface        = nullptr;
    vk::SampleCountFlagBits          msaaSamples    = vk::SampleCountFlagBits::e1;
    std::vector<char const*>         requiredDeviceExtensions = {vk::KHRSwapchainExtensionName};

    explicit VulkanContext(GLFWwindow* window);

private:
    void createInstance();
    void setupDebugMessenger();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    [[nodiscard]] bool isDeviceSuitable(vk::raii::PhysicalDevice const& dev) const;
    [[nodiscard]] vk::SampleCountFlagBits getMaxUsableSampleCount() const;
    static std::vector<char const*> getRequiredInstanceExtensions();
    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT,
        vk::DebugUtilsMessageTypeFlagsEXT,
        vk::DebugUtilsMessengerCallbackDataEXT const*,
        void*);
};
