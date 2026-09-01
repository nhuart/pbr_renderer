#include "application.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

bool Renderer::isDeviceSuitable(vk::raii::PhysicalDevice const& dev) {
    bool supportsVulkan14 = dev.getProperties().apiVersion >= vk::ApiVersion14;

    auto queueFamilies = dev.getQueueFamilyProperties();
    bool supportsGraphics = std::ranges::any_of(queueFamilies,
            [](auto const& qfp) { return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics); });

    auto availableDeviceExtensions = dev.enumerateDeviceExtensionProperties();
    bool supportsAllRequiredExtensions = std::ranges::all_of(requiredDeviceExtension,
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

void Renderer::pickPhysicalDevice() {
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

vk::SampleCountFlagBits Renderer::getMaxUsableSampleCount() const {
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

void Renderer::createLogicalDevice() {
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
        .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size()),
        .ppEnabledExtensionNames = requiredDeviceExtension.data(),
    };

    device = vk::raii::Device(physicalDevice, deviceCreateInfo);
    graphicsQueue = vk::raii::Queue(device, graphicsIndex, 0);
    computeQueue = vk::raii::Queue(device, graphicsIndex, 0);
    graphicsQueueFamilyIndex = graphicsIndex;

    std::cout << "Queues:\n";
    std::cout << "  graphics + present + compute (family " << graphicsIndex << ")\n";
}
