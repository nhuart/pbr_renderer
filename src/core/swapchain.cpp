#include "core/swapchain.hpp"
#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "core/vulkan_logging.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void logSwapchainSupport(vk::SurfaceCapabilitiesKHR const& capabilities,
        std::vector<vk::SurfaceFormatKHR> const& availableFormats,
        std::vector<vk::PresentModeKHR> const& availablePresentModes) {
    std::cout << "Swap chain support:\n";
    std::cout << "  image count: min=" << capabilities.minImageCount << " max="
              << (capabilities.maxImageCount == 0 ? std::string("unlimited")
                                                  : std::to_string(capabilities.maxImageCount))
              << "\n";
    std::cout << "  min extent: " << capabilities.minImageExtent.width << "x"
              << capabilities.minImageExtent.height << "\n";
    std::cout << "  max extent: " << capabilities.maxImageExtent.width << "x"
              << capabilities.maxImageExtent.height << "\n";
    std::cout << "  current extent: " << capabilities.currentExtent.width << "x"
              << capabilities.currentExtent.height << "\n";
    std::cout << "  supported transforms: " << vk::to_string(capabilities.supportedTransforms)
              << "\n";
    std::cout << "  current transform: " << vk::to_string(capabilities.currentTransform) << "\n";
    std::cout << "  supported composite alpha: "
              << vk::to_string(capabilities.supportedCompositeAlpha) << "\n";
    std::cout << "  supported usage flags: " << vk::to_string(capabilities.supportedUsageFlags)
              << "\n";
    std::cout << "  surface formats (" << availableFormats.size() << "):\n";
    for (auto const& format: availableFormats) {
        std::cout << "    " << vk::to_string(format.format) << " / "
                  << vk::to_string(format.colorSpace) << "\n";
    }
    std::cout << "  present modes (" << availablePresentModes.size() << "):\n";
    for (auto const& mode: availablePresentModes) {
        std::cout << "    " << vk::to_string(mode) << "\n";
    }
}

} // namespace

Swapchain::Swapchain(VulkanContext const& ctx, GLFWwindow* window) {
    create(ctx, window);
    createColorResources(ctx);
    createDepthResources(ctx);
}

void Swapchain::recreate(VulkanContext const& ctx, GLFWwindow* window) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }
    cleanup();
    create(ctx, window);
    createColorResources(ctx);
    createDepthResources(ctx);
}

void Swapchain::cleanup() {
    hdrSampler = nullptr;
    hdrImageView = nullptr;
    hdrImage = nullptr;
    hdrImageMemory = nullptr;
    colorImageView = nullptr;
    colorImage = nullptr;
    colorImageMemory = nullptr;
    depthImageView = nullptr;
    depthImage = nullptr;
    depthImageMemory = nullptr;
    imageViews.clear();
    swapChain = nullptr;
}

void Swapchain::create(VulkanContext const& ctx, GLFWwindow* window) {
    vk::SurfaceCapabilitiesKHR capabilities =
            ctx.physicalDevice.getSurfaceCapabilitiesKHR(*ctx.surface);
    auto availableFormats = ctx.physicalDevice.getSurfaceFormatsKHR(*ctx.surface);
    auto availablePresentModes = ctx.physicalDevice.getSurfacePresentModesKHR(*ctx.surface);

    if (vkutil::vulkanLoggingEnabled) {
        logSwapchainSupport(capabilities, availableFormats, availablePresentModes);
    }

    extent = chooseExtent(capabilities, window);
    surfaceFormat = chooseFormat(availableFormats);
    vk::PresentModeKHR presentMode = choosePresentMode(availablePresentModes);
    uint32_t imageCount = chooseMinImageCount(capabilities);

    vk::SwapchainCreateInfoKHR createInfo{
        .surface = *ctx.surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage =
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = presentMode,
        .clipped = true,
    };

    swapChain = vk::raii::SwapchainKHR(ctx.device, createInfo);
    images = swapChain.getImages();

    for (auto& image: images) {
        imageViews.push_back(vk::raii::ImageView(ctx.device,
                vk::ImageViewCreateInfo{
                    .image = image,
                    .viewType = vk::ImageViewType::e2D,
                    .format = surfaceFormat.format,
                    .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
                }));
    }

    if (vkutil::vulkanLoggingEnabled) {
        std::cout << "Swap chain:\n";
        std::cout << "  images: " << images.size() << " (requested min " << imageCount << ")\n";
        std::cout << "  format: " << vk::to_string(surfaceFormat.format) << " / "
                  << vk::to_string(surfaceFormat.colorSpace) << "\n";
        std::cout << "  extent: " << extent.width << "x" << extent.height << "\n";
        std::cout << "  present mode: " << vk::to_string(presentMode) << "\n";
        std::cout << "Image views: " << imageViews.size() << " created\n";
    }
}

void Swapchain::createColorResources(VulkanContext const& ctx) {
    if (ctx.msaaSamples != vk::SampleCountFlagBits::e1) {
        auto [image, memory] = vkutil::createImage(ctx, extent.width, extent.height, 1,
                ctx.msaaSamples, HDR_COLOR_FORMAT, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransientAttachment |
                        vk::ImageUsageFlagBits::eColorAttachment,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
        colorImage = std::move(image);
        colorImageMemory = std::move(memory);
        colorImageView = vkutil::createImageView(ctx, *colorImage, HDR_COLOR_FORMAT);
    }
    createHdrResources(ctx);
}

void Swapchain::createHdrResources(VulkanContext const& ctx) {
    auto [resolved, resolvedMemory] = vkutil::createImage(ctx, extent.width, extent.height, 1,
            vk::SampleCountFlagBits::e1, HDR_COLOR_FORMAT, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
    hdrImage = std::move(resolved);
    hdrImageMemory = std::move(resolvedMemory);
    hdrImageView = vkutil::createImageView(ctx, *hdrImage, HDR_COLOR_FORMAT);
    hdrSampler =
            vk::raii::Sampler(ctx.device, vk::SamplerCreateInfo{
                                              .magFilter = vk::Filter::eNearest,
                                              .minFilter = vk::Filter::eNearest,
                                              .mipmapMode = vk::SamplerMipmapMode::eNearest,
                                              .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                                              .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                                              .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                                          });
}

void Swapchain::createDepthResources(VulkanContext const& ctx) {
    vk::Format depthFormat = vkutil::findDepthFormat(ctx);
    auto [image, memory] = vkutil::createImage(ctx, extent.width, extent.height, 1, ctx.msaaSamples,
            depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
    depthImage = std::move(image);
    depthImageMemory = std::move(memory);
    depthImageView =
            vkutil::createImageView(ctx, *depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth);
}

vk::SurfaceFormatKHR Swapchain::chooseFormat(
        std::vector<vk::SurfaceFormatKHR> const& availableFormats) {
    auto found = std::ranges::find_if(availableFormats, [](auto const& fmt) {
        return fmt.format == vk::Format::eB8G8R8A8Srgb &&
               fmt.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });
    return found != availableFormats.end() ? *found : availableFormats[0];
}

vk::PresentModeKHR Swapchain::choosePresentMode(
        std::vector<vk::PresentModeKHR> const& availablePresentModes) {
    bool hasMailbox = std::ranges::any_of(availablePresentModes,
            [](auto mode) { return mode == vk::PresentModeKHR::eMailbox; });
    return hasMailbox ? vk::PresentModeKHR::eMailbox : vk::PresentModeKHR::eFifo;
}

vk::Extent2D Swapchain::chooseExtent(vk::SurfaceCapabilitiesKHR const& capabilities,
        GLFWwindow* window) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    return {
        std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width,
                capabilities.maxImageExtent.width),
        std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height,
                capabilities.maxImageExtent.height),
    };
}

uint32_t Swapchain::chooseMinImageCount(vk::SurfaceCapabilitiesKHR const& capabilities) {
    uint32_t count = std::max(3u, capabilities.minImageCount);
    if (capabilities.maxImageCount > 0 && capabilities.maxImageCount < count) {
        count = capabilities.maxImageCount;
    }
    return count;
}
