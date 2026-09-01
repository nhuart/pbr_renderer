#include "application.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

void Renderer::createSurface() {
  VkSurfaceKHR rawSurface = nullptr;
  if (glfwCreateWindowSurface(*instance, window, nullptr, &rawSurface) != VK_SUCCESS) {
    throw std::runtime_error("failed to create window surface!");
  }
  surface = vk::raii::SurfaceKHR(instance, rawSurface);
}

vk::SurfaceFormatKHR Renderer::chooseSwapSurfaceFormat(
    std::vector<vk::SurfaceFormatKHR> const& availableFormats) {
  auto found = std::ranges::find_if(availableFormats, [](auto const& fmt) {
    return fmt.format == vk::Format::eB8G8R8A8Srgb && fmt.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
  });
  return found != availableFormats.end() ? *found : availableFormats[0];
}

vk::PresentModeKHR Renderer::chooseSwapPresentMode(
    std::vector<vk::PresentModeKHR> const& availablePresentModes) {
  bool hasMailbox = std::ranges::any_of(availablePresentModes,
                                        [](auto mode) { return mode == vk::PresentModeKHR::eMailbox; });
  return hasMailbox ? vk::PresentModeKHR::eMailbox : vk::PresentModeKHR::eFifo;
}

vk::Extent2D Renderer::chooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities) {
  if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
    return capabilities.currentExtent;
  }
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  return {
      std::clamp(static_cast<uint32_t>(width),  capabilities.minImageExtent.width,  capabilities.maxImageExtent.width),
      std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
  };
}

uint32_t Renderer::chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& capabilities) {
  uint32_t count = std::max(3u, capabilities.minImageCount);
  if (capabilities.maxImageCount > 0 && capabilities.maxImageCount < count) {
    count = capabilities.maxImageCount;
  }
  return count;
}

void Renderer::createSwapChain() {
  vk::SurfaceCapabilitiesKHR capabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
  auto availableFormats      = physicalDevice.getSurfaceFormatsKHR(*surface);
  auto availablePresentModes = physicalDevice.getSurfacePresentModesKHR(*surface);

  std::cout << "Swap chain support:\n";
  std::cout << "  image count: min=" << capabilities.minImageCount << " max="
            << (capabilities.maxImageCount == 0 ? std::string("unlimited")
                                                : std::to_string(capabilities.maxImageCount))
            << "\n";
  std::cout << "  min extent: " << capabilities.minImageExtent.width << "x" << capabilities.minImageExtent.height << "\n";
  std::cout << "  max extent: " << capabilities.maxImageExtent.width << "x" << capabilities.maxImageExtent.height << "\n";
  std::cout << "  current extent: " << capabilities.currentExtent.width << "x" << capabilities.currentExtent.height << "\n";
  std::cout << "  supported transforms: " << vk::to_string(capabilities.supportedTransforms) << "\n";
  std::cout << "  current transform: " << vk::to_string(capabilities.currentTransform) << "\n";
  std::cout << "  supported composite alpha: " << vk::to_string(capabilities.supportedCompositeAlpha) << "\n";
  std::cout << "  supported usage flags: " << vk::to_string(capabilities.supportedUsageFlags) << "\n";
  std::cout << "  surface formats (" << availableFormats.size() << "):\n";
  for (auto const& fmt : availableFormats) {
    std::cout << "    " << vk::to_string(fmt.format) << " / " << vk::to_string(fmt.colorSpace) << "\n";
  }
  std::cout << "  present modes (" << availablePresentModes.size() << "):\n";
  for (auto const& mode : availablePresentModes) {
    std::cout << "    " << vk::to_string(mode) << "\n";
  }

  swapChainExtent        = chooseSwapExtent(capabilities);
  swapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);
  vk::PresentModeKHR presentMode = chooseSwapPresentMode(availablePresentModes);
  uint32_t imageCount = chooseSwapMinImageCount(capabilities);

  vk::SwapchainCreateInfoKHR createInfo{
      .surface          = *surface,
      .minImageCount    = imageCount,
      .imageFormat      = swapChainSurfaceFormat.format,
      .imageColorSpace  = swapChainSurfaceFormat.colorSpace,
      .imageExtent      = swapChainExtent,
      .imageArrayLayers = 1,
      .imageUsage       = vk::ImageUsageFlagBits::eColorAttachment,
      .imageSharingMode = vk::SharingMode::eExclusive,
      .preTransform     = capabilities.currentTransform,
      .compositeAlpha   = vk::CompositeAlphaFlagBitsKHR::eOpaque,
      .presentMode      = presentMode,
      .clipped          = true,
  };

  swapChain       = vk::raii::SwapchainKHR(device, createInfo);
  swapChainImages = swapChain.getImages();

  std::cout << "Swap chain:\n";
  std::cout << "  images: " << swapChainImages.size() << " (requested min " << imageCount << ")\n";
  std::cout << "  format: " << vk::to_string(swapChainSurfaceFormat.format) << " / "
            << vk::to_string(swapChainSurfaceFormat.colorSpace) << "\n";
  std::cout << "  extent: " << swapChainExtent.width << "x" << swapChainExtent.height << "\n";
  std::cout << "  present mode: " << vk::to_string(presentMode) << "\n";
}

void Renderer::createImageViews() {
  for (auto& image : swapChainImages) {
    swapChainImageViews.push_back(createImageView(image, swapChainSurfaceFormat.format));
  }
  std::cout << "Image views: " << swapChainImageViews.size() << " created\n";
}

void Renderer::cleanupSwapChain() {
  colorImageView = nullptr;
  colorImage     = nullptr;
  colorImageMemory = nullptr;
  depthImageView = nullptr;
  depthImage     = nullptr;
  depthImageMemory = nullptr;
  swapChainImageViews.clear();
  swapChain = nullptr;
}

void Renderer::recreateSwapChain() {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  while (width == 0 || height == 0) {
    glfwGetFramebufferSize(window, &width, &height);
    glfwWaitEvents();
  }

  device.waitIdle();
  cleanupSwapChain();
  createSwapChain();
  createImageViews();
  createColorResources();
  createDepthResources();

  renderFinishedSemaphores.clear();
  for (size_t i = 0; i < swapChainImages.size(); i++) {
    renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo{});
  }
}

void Renderer::framebufferResizeCallback(GLFWwindow* window, int /*width*/, int /*height*/) {
  auto* app = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
  app->framebufferResized = true;
}
