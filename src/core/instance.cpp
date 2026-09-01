#include "application.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

void Renderer::createInstance() {
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
  auto unsupportedLayerIt = std::ranges::find_if(requiredLayers, [&layerProperties](auto const& requiredLayer) {
    return std::ranges::none_of(layerProperties, [requiredLayer](auto const& layerProperty) {
      return strcmp(layerProperty.layerName, requiredLayer) == 0;
    });
  });
  if (unsupportedLayerIt != requiredLayers.end()) {
    throw std::runtime_error("Required layer not supported: " + std::string(*unsupportedLayerIt));
  }

  auto requiredExtensions = getRequiredInstanceExtensions();

  auto extensionProperties = context.enumerateInstanceExtensionProperties();
  auto unsupportedPropertyIt =
      std::ranges::find_if(requiredExtensions, [&extensionProperties](auto const& requiredExtension) {
        return std::ranges::none_of(extensionProperties, [requiredExtension](auto const& extensionProperty) {
          return strcmp(extensionProperty.extensionName, requiredExtension) == 0;
        });
      });
  if (unsupportedPropertyIt != requiredExtensions.end()) {
    throw std::runtime_error("Required extension not supported: " + std::string(*unsupportedPropertyIt));
  }

  std::cout << "Enabled layers:\n";
  for (auto const& layer : requiredLayers) {
    std::cout << "  " << layer << "\n";
  }
  std::cout << "Enabled extensions:\n";
  for (auto const& ext : requiredExtensions) {
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

void Renderer::setupDebugMessenger() {
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

std::vector<const char*> Renderer::getRequiredInstanceExtensions() {
  uint32_t glfwExtensionCount = 0;
  auto* glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
  if (ENABLE_VALIDATION_LAYERS) {
    extensions.push_back(vk::EXTDebugUtilsExtensionName);
  }
  return extensions;
}

VKAPI_ATTR vk::Bool32 VKAPI_CALL Renderer::debugCallback(
  vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
  vk::DebugUtilsMessageTypeFlagsEXT type,
  const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
  void* /*unused*/
) {
  if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError ||
      severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
    std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << "\n";
  }
  return vk::False;
}
