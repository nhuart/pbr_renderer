#include "core/context.hpp"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

VulkanContext::VulkanContext(GLFWwindow* window) {
    createInstance();
    setupDebugMessenger();
    createSurface(window);
    pickPhysicalDevice();
    createLogicalDevice();
}

void VulkanContext::createInstance() {
    constexpr vk::ApplicationInfo APP_INFO{
        .pApplicationName = "PBR Renderer",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = vk::ApiVersion14,
    };

    std::vector<char const*> requiredLayers;
    if (ENABLE_VALIDATION_LAYERS) {
        requiredLayers.assign(VALIDATION_LAYERS.begin(), VALIDATION_LAYERS.end());
    }

    auto layerProperties = context.enumerateInstanceLayerProperties();
    auto unsupportedLayerIt =
            std::ranges::find_if(requiredLayers, [&layerProperties](auto const& requiredLayer) {
                return std::ranges::none_of(layerProperties,
                        [requiredLayer](auto const& layerProperty) {
                            return strcmp(layerProperty.layerName, requiredLayer) == 0;
                        });
            });
    if (unsupportedLayerIt != requiredLayers.end()) {
        throw std::runtime_error(
                "Required layer not supported: " + std::string(*unsupportedLayerIt));
    }

    auto requiredExtensions = getRequiredInstanceExtensions();

    auto extensionProperties = context.enumerateInstanceExtensionProperties();
    auto unsupportedPropertyIt = std::ranges::find_if(requiredExtensions,
            [&extensionProperties](auto const& requiredExtension) {
                return std::ranges::none_of(extensionProperties,
                        [requiredExtension](auto const& extensionProperty) {
                            return strcmp(extensionProperty.extensionName, requiredExtension) == 0;
                        });
            });
    if (unsupportedPropertyIt != requiredExtensions.end()) {
        throw std::runtime_error(
                "Required extension not supported: " + std::string(*unsupportedPropertyIt));
    }

    std::cout << "Enabled layers:\n";
    for (auto const& layer: requiredLayers) {
        std::cout << "  " << layer << "\n";
    }
    std::cout << "Enabled extensions:\n";
    for (auto const& ext: requiredExtensions) {
        std::cout << "  " << ext << "\n";
    }

    vk::InstanceCreateInfo createInfo{
        .pApplicationInfo = &APP_INFO,
        .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
        .ppEnabledLayerNames = requiredLayers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
        .ppEnabledExtensionNames = requiredExtensions.data(),
    };
    instance = vk::raii::Instance(context, createInfo);
}

void VulkanContext::setupDebugMessenger() {
    if (!ENABLE_VALIDATION_LAYERS) {
        return;
    }

    vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{
        .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
        .pfnUserCallback = &debugCallback,
    };
    debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
}

void VulkanContext::createSurface(GLFWwindow* window) {
    VkSurfaceKHR rawSurface = nullptr;
    if (glfwCreateWindowSurface(*instance, window, nullptr, &rawSurface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
    surface = vk::raii::SurfaceKHR(instance, rawSurface);
}

std::vector<char const*> VulkanContext::getRequiredInstanceExtensions() {
    uint32_t glfwExtensionCount = 0;
    auto* glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    if (ENABLE_VALIDATION_LAYERS) {
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
    }
    return extensions;
}

VKAPI_ATTR vk::Bool32 VKAPI_CALL VulkanContext::debugCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type,
        vk::DebugUtilsMessengerCallbackDataEXT const* pCallbackData, void* /*unused*/
) {
    if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError ||
            severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
        std::cerr << "validation layer: type " << to_string(type)
                  << " msg: " << pCallbackData->pMessage << "\n";
    }
    return vk::False;
}

bool VulkanContext::isDeviceSuitable(vk::raii::PhysicalDevice const& dev) const {
    bool supportsVulkan14 = dev.getProperties().apiVersion >= vk::ApiVersion14;

    auto queueFamilies = dev.getQueueFamilyProperties();
    bool supportsGraphics = std::ranges::any_of(queueFamilies,
            [](auto const& qfp) { return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics); });

    auto availableDeviceExtensions = dev.enumerateDeviceExtensionProperties();
    bool supportsAllRequiredExtensions = std::ranges::all_of(requiredDeviceExtensions,
            [&availableDeviceExtensions](auto const& requiredExt) {
                return std::ranges::any_of(availableDeviceExtensions,
                        [requiredExt](auto const& availableExt) {
                            return strcmp(availableExt.extensionName, requiredExt) == 0;
                        });
            });

    auto features = dev.template getFeatures2<vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
    bool supportsRequiredFeatures =
            features.template get<vk::PhysicalDeviceFeatures2>().features.sampleRateShading &&
            features.template get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy &&
            features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
            features.template get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
            features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
            features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>()
                    .extendedDynamicState;

    return supportsVulkan14 && supportsGraphics && supportsAllRequiredExtensions &&
           supportsRequiredFeatures;
}

void VulkanContext::pickPhysicalDevice() {
    std::vector<vk::raii::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();

    std::cout << "Available GPUs:\n";
    for (auto const& gpu: physicalDevices) {
        auto const& props = gpu.getProperties();
        bool suitable = isDeviceSuitable(gpu);
        std::cout << "  " << props.deviceName << " [" << vk::to_string(props.deviceType) << "]"
                  << (suitable ? " (suitable)" : " (not suitable)") << "\n";
    }

    auto const DEVICE_ITER = std::ranges::find_if(physicalDevices,
            [&](auto const& gpu) { return isDeviceSuitable(gpu); });
    if (DEVICE_ITER == physicalDevices.end()) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
    physicalDevice = *DEVICE_ITER;
    msaaSamples = getMaxUsableSampleCount();
    std::cout << "Selected GPU: " << physicalDevice.getProperties().deviceName << "\n";
    std::cout << "MSAA samples: " << vk::to_string(msaaSamples) << "\n";
}

vk::SampleCountFlagBits VulkanContext::getMaxUsableSampleCount() const {
    vk::SampleCountFlags counts =
            physicalDevice.getProperties().limits.framebufferColorSampleCounts &
            physicalDevice.getProperties().limits.framebufferDepthSampleCounts;
    for (auto candidate: { vk::SampleCountFlagBits::e64, vk::SampleCountFlagBits::e32,
             vk::SampleCountFlagBits::e16, vk::SampleCountFlagBits::e8, vk::SampleCountFlagBits::e4,
             vk::SampleCountFlagBits::e2 }) {
        if (counts & candidate) {
            return candidate;
        }
    }
    return vk::SampleCountFlagBits::e1;
}

void VulkanContext::createLogicalDevice() {
    std::vector<vk::QueueFamilyProperties> queueFamilyProperties =
            physicalDevice.getQueueFamilyProperties();

    uint32_t graphicsIndex = std::numeric_limits<uint32_t>::max();
    for (uint32_t i = 0; i < queueFamilyProperties.size(); ++i) {
        bool hasGraphics = !!(queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics);
        bool hasPresent = physicalDevice.getSurfaceSupportKHR(i, *surface);
        if (hasGraphics && hasPresent) {
            graphicsIndex = i;
            break;
        }
    }
    if (graphicsIndex == std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("no queue family supports both graphics and presentation!");
    }

    vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
            featureChain = {
                { .features = { .sampleRateShading = true, .samplerAnisotropy = true } },
                { .shaderDrawParameters = true },
                { .synchronization2 = true, .dynamicRendering = true },
                { .extendedDynamicState = true },
            };

    float queuePriority = 0.5f;
    vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
        .queueFamilyIndex = graphicsIndex,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };
    vk::DeviceCreateInfo deviceCreateInfo{
        .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &deviceQueueCreateInfo,
        .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size()),
        .ppEnabledExtensionNames = requiredDeviceExtensions.data(),
    };

    device = vk::raii::Device(physicalDevice, deviceCreateInfo);
    graphicsQueue = vk::raii::Queue(device, graphicsIndex, 0);
    computeQueue = vk::raii::Queue(device, graphicsIndex, 0);
    graphicsQueueFamilyIndex = graphicsIndex;

    std::cout << "Queues:\n";
    std::cout << "  graphics + present + compute (family " << graphicsIndex << ")\n";
}
