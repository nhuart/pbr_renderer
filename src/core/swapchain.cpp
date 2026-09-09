#include "core/swapchain.hpp"
#include "core/context.hpp"
#include "core/resource_allocator.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

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
    for (auto const& fmt: availableFormats) {
        std::cout << "    " << vk::to_string(fmt.format) << " / " << vk::to_string(fmt.colorSpace)
                  << "\n";
    }
    std::cout << "  present modes (" << availablePresentModes.size() << "):\n";
    for (auto const& mode: availablePresentModes) {
        std::cout << "    " << vk::to_string(mode) << "\n";
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

    std::cout << "Swap chain:\n";
    std::cout << "  images: " << images.size() << " (requested min " << imageCount << ")\n";
    std::cout << "  format: " << vk::to_string(surfaceFormat.format) << " / "
              << vk::to_string(surfaceFormat.colorSpace) << "\n";
    std::cout << "  extent: " << extent.width << "x" << extent.height << "\n";
    std::cout << "  present mode: " << vk::to_string(presentMode) << "\n";
    std::cout << "Image views: " << imageViews.size() << " created\n";
}

static std::pair<vk::raii::Image, vk::raii::DeviceMemory> createImageLocal(VulkanContext const& ctx,
        uint32_t width, uint32_t height, uint32_t mipLevels, vk::SampleCountFlagBits samples,
        vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage,
        vk::MemoryPropertyFlags properties) {
    vk::raii::Image image(ctx.device, vk::ImageCreateInfo{
                                          .imageType = vk::ImageType::e2D,
                                          .format = format,
                                          .extent = { width, height, 1 },
                                          .mipLevels = mipLevels,
                                          .arrayLayers = 1,
                                          .samples = samples,
                                          .tiling = tiling,
                                          .usage = usage,
                                          .sharingMode = vk::SharingMode::eExclusive,
                                          .initialLayout = vk::ImageLayout::eUndefined,
                                      });
    vk::MemoryRequirements memReq = image.getMemoryRequirements();
    vk::raii::DeviceMemory memory(ctx.device,
            vk::MemoryAllocateInfo{
                .allocationSize = memReq.size,
                .memoryTypeIndex = vkutil::findMemoryType(ctx, memReq.memoryTypeBits, properties),
            });
    image.bindMemory(*memory, 0);
    return { std::move(image), std::move(memory) };
}

static vk::raii::ImageView createImageViewLocal(VulkanContext const& ctx, vk::Image image,
        vk::Format format, vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor,
        uint32_t mipLevels = 1) {
    return vk::raii::ImageView(ctx.device,
            vk::ImageViewCreateInfo{
                .image = image,
                .viewType = vk::ImageViewType::e2D,
                .format = format,
                .subresourceRange = { aspectFlags, 0, mipLevels, 0, 1 },
            });
}

static vk::Format findDepthFormatLocal(VulkanContext const& ctx) {
    for (vk::Format format:
            { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint }) {
        vk::FormatProperties props = ctx.physicalDevice.getFormatProperties(format);
        if ((props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) ==
                vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
            return format;
        }
    }
    throw std::runtime_error("failed to find supported depth format!");
}

void Swapchain::createColorResources(VulkanContext const& ctx) {
    auto [img, mem] = createImageLocal(ctx, extent.width, extent.height, 1, ctx.msaaSamples,
            surfaceFormat.format, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
    colorImage = std::move(img);
    colorImageMemory = std::move(mem);
    colorImageView = createImageViewLocal(ctx, *colorImage, surfaceFormat.format);
}

void Swapchain::createDepthResources(VulkanContext const& ctx) {
    vk::Format depthFormat = findDepthFormatLocal(ctx);
    auto [img, mem] = createImageLocal(ctx, extent.width, extent.height, 1, ctx.msaaSamples,
            depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
    depthImage = std::move(img);
    depthImageMemory = std::move(mem);
    depthImageView =
            createImageViewLocal(ctx, *depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth);
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
