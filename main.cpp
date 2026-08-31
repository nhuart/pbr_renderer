#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <ktx.h>
#include <ktxvulkan.h>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#define GLFW_INCLUDE_VULKAN  // REQUIRED only for GLFW CreateWindowSurface.
#include <GLFW/glfw3.h>

#include <chrono>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

struct Vertex {
  glm::vec3 pos;
  glm::vec3 color;
  glm::vec2 texCoord;

  bool operator==(Vertex const& other) const {
    return pos == other.pos && color == other.color && texCoord == other.texCoord;
  }

  static vk::VertexInputBindingDescription getBindingDescription() {
    return {.binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex};
  }

  static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions() {
    return {{{.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, pos)},
             {.location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, color)},
             {.location = 2, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(Vertex, texCoord)}}};
  }
};

namespace std {
template <>
struct hash<Vertex> {
  size_t operator()(Vertex const& vtx) const {
    return ((hash<glm::vec3>()(vtx.pos) ^ (hash<glm::vec3>()(vtx.color) << 1)) >> 1) ^
           (hash<glm::vec2>()(vtx.texCoord) << 1);
  }
};
}  // namespace std

struct UniformBufferObject {
  alignas(16) glm::mat4 model;
  alignas(16) glm::mat4 view;
  alignas(16) glm::mat4 proj;
};

struct Particle {
  glm::vec2 position;
  glm::vec2 velocity;
  glm::vec4 color;

  static vk::VertexInputBindingDescription getBindingDescription() {
    return {.binding = 0, .stride = sizeof(Particle), .inputRate = vk::VertexInputRate::eVertex};
  }

  static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions() {
    return {{{.location = 0, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(Particle, position)},
             {.location = 1,
              .binding = 0,
              .format = vk::Format::eR32G32B32A32Sfloat,
              .offset = offsetof(Particle, color)}}};
  }
};

struct ComputeUBO {
  float deltaTime;
};

const std::string MODEL_PATH = "models/viking_room.glb";
const std::string TEXTURE_PATH = "textures/viking_room.ktx2";

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;
constexpr uint32_t PARTICLE_COUNT = 8192;

const std::vector<char const*> VALIDATION_LAYERS = {"VK_LAYER_KHRONOS_validation"};

#ifdef NDEBUG
constexpr bool ENABLE_VALIDATION_LAYERS = false;
#else
constexpr bool ENABLE_VALIDATION_LAYERS = true;
#endif

class HelloTriangleApplication {
 public:
  void run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
  }

 private:
  GLFWwindow* window = nullptr;

  vk::raii::Context context;
  vk::raii::Instance instance = nullptr;
  vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;

  vk::raii::PhysicalDevice physicalDevice = nullptr;
  vk::raii::Device device = nullptr;

  vk::raii::Queue graphicsQueue = nullptr;
  vk::raii::Queue computeQueue = nullptr;
  uint32_t graphicsQueueFamilyIndex = 0;

  vk::raii::SurfaceKHR surface = nullptr;

  vk::raii::SwapchainKHR swapChain = nullptr;
  std::vector<vk::Image> swapChainImages;
  vk::SurfaceFormatKHR swapChainSurfaceFormat;
  vk::Extent2D swapChainExtent;
  std::vector<vk::raii::ImageView> swapChainImageViews;

  vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
  vk::raii::PipelineLayout pipelineLayout = nullptr;

  vk::raii::DescriptorPool descriptorPool = nullptr;
  std::vector<vk::raii::DescriptorSet> descriptorSets;
  vk::raii::Pipeline graphicsPipeline = nullptr;

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

  vk::raii::Pipeline particlePipeline = nullptr;
  vk::raii::PipelineLayout particlePipelineLayout = nullptr;

  std::vector<vk::raii::Semaphore> computeFinishedSemaphores;
  std::vector<vk::raii::Fence> computeInFlightFences;

  std::vector<vk::raii::Buffer> uniformBuffers;
  std::vector<vk::raii::DeviceMemory> uniformBuffersMemory;
  std::vector<void*> uniformBuffersMapped;

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  vk::raii::Buffer vertexBuffer = nullptr;
  vk::raii::DeviceMemory vertexBufferMemory = nullptr;

  vk::raii::Buffer indexBuffer = nullptr;
  vk::raii::DeviceMemory indexBufferMemory = nullptr;

  vk::raii::CommandPool commandPool = nullptr;
  std::vector<vk::raii::CommandBuffer> commandBuffers;
  std::vector<vk::raii::CommandBuffer> computeCommandBuffers;

  vk::raii::Image depthImage = nullptr;
  vk::raii::DeviceMemory depthImageMemory = nullptr;
  vk::raii::ImageView depthImageView = nullptr;

  vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1;

  vk::raii::Image colorImage = nullptr;
  vk::raii::DeviceMemory colorImageMemory = nullptr;
  vk::raii::ImageView colorImageView = nullptr;

  uint32_t mipLevels = 0;
  vk::Format textureFormat = vk::Format::eR8G8B8A8Srgb;
  vk::raii::Image textureImage = nullptr;
  vk::raii::DeviceMemory textureImageMemory = nullptr;
  vk::raii::ImageView textureImageView = nullptr;
  vk::raii::Sampler textureSampler = nullptr;

  std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
  std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
  std::vector<vk::raii::Fence> inFlightFences;

  uint32_t frameIndex = 0;
  bool framebufferResized = false;

  std::vector<const char*> requiredDeviceExtension = {vk::KHRSwapchainExtensionName};

  void initWindow() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
  }

  void initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createDescriptorSetLayout();
    createComputeDescriptorSetLayout();
    createGraphicsPipeline();
    createParticlePipeline();
    createComputePipeline();
    createCommandPool();
    createColorResources();
    createDepthResources();
    createTextureImage();
    createTextureImageView();
    createTextureSampler();
    loadModel();
    createVertexBuffer();
    createIndexBuffer();
    createUniformBuffers();
    createShaderStorageBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createComputeDescriptorSets();
    createCommandBuffers();
    createComputeCommandBuffers();
    createSyncObjects();
  }

  void mainLoop() {
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();
      drawFrame();
    }
    device.waitIdle();
  }

  void cleanup() {
    cleanupSwapChain();
    glfwDestroyWindow(window);
    glfwTerminate();
  }

  void createInstance() {
    constexpr vk::ApplicationInfo APP_INFO{.pApplicationName = "Hello Triangle",
                                           .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
                                           .pEngineName = "No Engine",
                                           .engineVersion = VK_MAKE_VERSION(1, 0, 0),
                                           .apiVersion = vk::ApiVersion14};

    // Get the required layers
    std::vector<char const*> requiredLayers;
    if (ENABLE_VALIDATION_LAYERS) {
      requiredLayers.assign(VALIDATION_LAYERS.begin(), VALIDATION_LAYERS.end());
    }

    // Check if the required layers are supported by the Vulkan implementation.
    auto layerProperties = context.enumerateInstanceLayerProperties();
    auto unsupportedLayerIt = std::ranges::find_if(requiredLayers, [&layerProperties](auto const& requiredLayer) {
      return std::ranges::none_of(layerProperties, [requiredLayer](auto const& layerProperty) {
        return strcmp(layerProperty.layerName, requiredLayer) == 0;
      });
    });
    if (unsupportedLayerIt != requiredLayers.end()) {
      throw std::runtime_error("Required layer not supported: " + std::string(*unsupportedLayerIt));
    }

    // Get the required extensions.
    auto requiredExtensions = getRequiredInstanceExtensions();

    // Check if the required extensions are supported by the Vulkan implementation.
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

    vk::InstanceCreateInfo createInfo{.pApplicationInfo = &APP_INFO,
                                      .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
                                      .ppEnabledLayerNames = requiredLayers.data(),
                                      .enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
                                      .ppEnabledExtensionNames = requiredExtensions.data()};
    instance = vk::raii::Instance(context, createInfo);
  }

  void setupDebugMessenger() {
    if (!ENABLE_VALIDATION_LAYERS) {
      return;
    }

    vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                                        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
    vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                                                       vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                                                       vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
    vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{
        .messageSeverity = severityFlags, .messageType = messageTypeFlags, .pfnUserCallback = &debugCallback};
    debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
  }

  void createSurface() {
    VkSurfaceKHR rawSurface = nullptr;
    if (glfwCreateWindowSurface(*instance, window, nullptr, &rawSurface) != VK_SUCCESS) {
      throw std::runtime_error("failed to create window surface!");
    }
    surface = vk::raii::SurfaceKHR(instance, rawSurface);
  }

  bool isDeviceSuitable(vk::raii::PhysicalDevice const& device) {
    // Check if the device supports the Vulkan 1.4 API version
    bool supportsVulkan14 = device.getProperties().apiVersion >= vk::ApiVersion14;

    // Check if any of the queue families support graphics operations
    auto queueFamilies = device.getQueueFamilyProperties();
    bool supportsGraphics = std::ranges::any_of(
        queueFamilies, [](auto const& qfp) { return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics); });

    // Check if all required device extensions are available
    auto availableDeviceExtensions = device.enumerateDeviceExtensionProperties();
    bool supportsAllRequiredExtensions =
        std::ranges::all_of(requiredDeviceExtension, [&availableDeviceExtensions](auto const& requiredExt) {
          return std::ranges::any_of(availableDeviceExtensions, [requiredExt](auto const& availableExt) {
            return strcmp(availableExt.extensionName, requiredExt) == 0;
          });
        });

    // Check if the device supports the required features
    auto features = device.template getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
                                                 vk::PhysicalDeviceVulkan13Features,
                                                 vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
    bool supportsRequiredFeatures =
        features.template get<vk::PhysicalDeviceFeatures2>().features.sampleRateShading &&
        features.template get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy &&
        features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
        features.template get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
        features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
        features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

    return supportsVulkan14 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
  }

  void pickPhysicalDevice() {
    std::vector<vk::raii::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();

    std::cout << "Available GPUs:\n";
    for (auto const& gpu : physicalDevices) {
      auto const& props = gpu.getProperties();
      bool suitable = isDeviceSuitable(gpu);
      std::cout << "  " << props.deviceName << " [" << vk::to_string(props.deviceType) << "]"
                << (suitable ? " (suitable)" : " (not suitable)") << "\n";
    }

    auto const DEVICE_ITER =
        std::ranges::find_if(physicalDevices, [&](auto const& gpu) { return isDeviceSuitable(gpu); });
    if (DEVICE_ITER == physicalDevices.end()) {
      throw std::runtime_error("failed to find a suitable GPU!");
    }
    physicalDevice = *DEVICE_ITER;
    msaaSamples = getMaxUsableSampleCount();
    std::cout << "Selected GPU: " << physicalDevice.getProperties().deviceName << "\n";
    std::cout << "MSAA samples: " << vk::to_string(msaaSamples) << "\n";
  }

  [[nodiscard]] vk::SampleCountFlagBits getMaxUsableSampleCount() const {
    vk::SampleCountFlags counts = physicalDevice.getProperties().limits.framebufferColorSampleCounts &
                                  physicalDevice.getProperties().limits.framebufferDepthSampleCounts;
    for (auto candidate : {vk::SampleCountFlagBits::e64, vk::SampleCountFlagBits::e32, vk::SampleCountFlagBits::e16,
                           vk::SampleCountFlagBits::e8, vk::SampleCountFlagBits::e4, vk::SampleCountFlagBits::e2}) {
      if (counts & candidate) {
        return candidate;
      }
    }
    return vk::SampleCountFlagBits::e1;
  }

  static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const& availableFormats) {
    auto found = std::ranges::find_if(availableFormats, [](auto const& fmt) {
      return fmt.format == vk::Format::eB8G8R8A8Srgb && fmt.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });
    return found != availableFormats.end() ? *found : availableFormats[0];
  }

  static vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const& availablePresentModes) {
    bool hasMailbox =
        std::ranges::any_of(availablePresentModes, [](auto mode) { return mode == vk::PresentModeKHR::eMailbox; });
    return hasMailbox ? vk::PresentModeKHR::eMailbox : vk::PresentModeKHR::eFifo;
  }

  vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
      return capabilities.currentExtent;
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    return {
        std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height),
    };
  }

  static uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& capabilities) {
    uint32_t count = std::max(3u, capabilities.minImageCount);
    if (capabilities.maxImageCount > 0 && capabilities.maxImageCount < count) {
      count = capabilities.maxImageCount;
    }
    return count;
  }

  void cleanupSwapChain() {
    colorImageView = nullptr;
    colorImage = nullptr;
    colorImageMemory = nullptr;
    depthImageView = nullptr;
    depthImage = nullptr;
    depthImageMemory = nullptr;
    swapChainImageViews.clear();
    swapChain = nullptr;
  }

  void recreateSwapChain() {
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

  void createSwapChain() {
    vk::SurfaceCapabilitiesKHR capabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
    auto availableFormats = physicalDevice.getSurfaceFormatsKHR(*surface);
    auto availablePresentModes = physicalDevice.getSurfacePresentModesKHR(*surface);

    std::cout << "Swap chain support:\n";
    std::cout << "  image count: min=" << capabilities.minImageCount << " max="
              << (capabilities.maxImageCount == 0 ? std::string("unlimited")
                                                  : std::to_string(capabilities.maxImageCount))
              << "\n";
    std::cout << "  min extent: " << capabilities.minImageExtent.width << "x" << capabilities.minImageExtent.height
              << "\n";
    std::cout << "  max extent: " << capabilities.maxImageExtent.width << "x" << capabilities.maxImageExtent.height
              << "\n";
    std::cout << "  current extent: " << capabilities.currentExtent.width << "x" << capabilities.currentExtent.height
              << "\n";
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

    swapChainExtent = chooseSwapExtent(capabilities);
    swapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);
    vk::PresentModeKHR presentMode = chooseSwapPresentMode(availablePresentModes);
    uint32_t imageCount = chooseSwapMinImageCount(capabilities);

    vk::SwapchainCreateInfoKHR createInfo{
        .surface = *surface,
        .minImageCount = imageCount,
        .imageFormat = swapChainSurfaceFormat.format,
        .imageColorSpace = swapChainSurfaceFormat.colorSpace,
        .imageExtent = swapChainExtent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = presentMode,
        .clipped = true,
    };

    swapChain = vk::raii::SwapchainKHR(device, createInfo);
    swapChainImages = swapChain.getImages();

    std::cout << "Swap chain:\n";
    std::cout << "  images: " << swapChainImages.size() << " (requested min " << imageCount << ")\n";
    std::cout << "  format: " << vk::to_string(swapChainSurfaceFormat.format) << " / "
              << vk::to_string(swapChainSurfaceFormat.colorSpace) << "\n";
    std::cout << "  extent: " << swapChainExtent.width << "x" << swapChainExtent.height << "\n";
    std::cout << "  present mode: " << vk::to_string(presentMode) << "\n";
  }

  [[nodiscard]] vk::raii::ImageView createImageView(vk::Image image, vk::Format format,
                                                    vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor,
                                                    uint32_t numMipLevels = 1) const {
    return vk::raii::ImageView(device, vk::ImageViewCreateInfo{
                                           .image = image,
                                           .viewType = vk::ImageViewType::e2D,
                                           .format = format,
                                           .subresourceRange = {aspectFlags, 0, numMipLevels, 0, 1},
                                       });
  }

  void createImageViews() {
    for (auto& image : swapChainImages) {
      swapChainImageViews.push_back(createImageView(image, swapChainSurfaceFormat.format));
    }
    std::cout << "Image views: " << swapChainImageViews.size() << " created\n";
  }

  void createTextureImageView() {
    textureImageView = createImageView(*textureImage, textureFormat, vk::ImageAspectFlagBits::eColor, mipLevels);
  }

  [[nodiscard]] vk::Format findSupportedFormat(std::vector<vk::Format> const& candidates, vk::ImageTiling tiling,
                                               vk::FormatFeatureFlags features) const {
    for (vk::Format format : candidates) {
      vk::FormatProperties props = physicalDevice.getFormatProperties(format);
      if ((tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) ||
          (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features)) {
        return format;
      }
    }
    throw std::runtime_error("failed to find supported format!");
  }

  [[nodiscard]] vk::Format findDepthFormat() const {
    return findSupportedFormat({vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
                               vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);
  }

  void createColorResources() {
    std::tie(colorImage, colorImageMemory) =
        createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, swapChainSurfaceFormat.format,
                    vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment,
                    vk::MemoryPropertyFlagBits::eDeviceLocal);
    colorImageView = createImageView(*colorImage, swapChainSurfaceFormat.format);
  }

  void createDepthResources() {
    vk::Format depthFormat = findDepthFormat();
    std::tie(depthImage, depthImageMemory) = createImage(
        swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, depthFormat, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
    depthImageView = createImageView(*depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth);
  }

  void createTextureSampler() {
    vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
    textureSampler = vk::raii::Sampler(device, vk::SamplerCreateInfo{
                                                   .magFilter = vk::Filter::eLinear,
                                                   .minFilter = vk::Filter::eLinear,
                                                   .mipmapMode = vk::SamplerMipmapMode::eLinear,
                                                   .addressModeU = vk::SamplerAddressMode::eRepeat,
                                                   .addressModeV = vk::SamplerAddressMode::eRepeat,
                                                   .addressModeW = vk::SamplerAddressMode::eRepeat,
                                                   .mipLodBias = 0.0f,
                                                   .anisotropyEnable = vk::True,
                                                   .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
                                                   .compareEnable = vk::False,
                                                   .compareOp = vk::CompareOp::eAlways,
                                                   .minLod = 0.0f,
                                                   .maxLod = vk::LodClampNone,
                                                   .borderColor = vk::BorderColor::eIntOpaqueBlack,
                                                   .unnormalizedCoordinates = vk::False,
                                               });
    std::cout << "Texture sampler: created (max anisotropy: " << properties.limits.maxSamplerAnisotropy << ")\n";
  }

  void createGraphicsPipeline() {
    auto vertCode = readFile("shaders/compiled/triangle.vert.spv");
    auto fragCode = readFile("shaders/compiled/triangle.frag.spv");

    vk::raii::ShaderModule vertModule = createShaderModule(vertCode);
    vk::raii::ShaderModule fragModule = createShaderModule(fragCode);

    vk::PipelineShaderStageCreateInfo vertStageInfo{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = *vertModule,
        .pName = "main",
    };
    vk::PipelineShaderStageCreateInfo fragStageInfo{
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = *fragModule,
        .pName = "main",
    };

    std::array shaderStages = {vertStageInfo, fragStageInfo};

    std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
        .pVertexAttributeDescriptions = attributeDescriptions.data(),
    };

    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
        .topology = vk::PrimitiveTopology::eTriangleList,
        .primitiveRestartEnable = vk::False,
    };

    vk::PipelineViewportStateCreateInfo viewportStateInfo{
        .viewportCount = 1,
        .scissorCount = 1,
    };

    vk::PipelineRasterizationStateCreateInfo rasterizerInfo{
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0f,
    };

    vk::PipelineMultisampleStateCreateInfo multisamplingInfo{
        .rasterizationSamples = msaaSamples,
        .sampleShadingEnable = vk::True,
        .minSampleShading = 0.2f,
    };

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    vk::PipelineColorBlendStateCreateInfo colorBlendingInfo{
        .logicOpEnable = vk::False,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
    };

    vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False,
    };

    vk::DescriptorSetLayout dslHandle = *descriptorSetLayout;
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &dslHandle,
        .pushConstantRangeCount = 0,
    };
    pipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

    vk::Format depthFormat = findDepthFormat();
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain = {
        {
            .stageCount = static_cast<uint32_t>(shaderStages.size()),
            .pStages = shaderStages.data(),
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssemblyInfo,
            .pViewportState = &viewportStateInfo,
            .pRasterizationState = &rasterizerInfo,
            .pMultisampleState = &multisamplingInfo,
            .pDepthStencilState = &depthStencilInfo,
            .pColorBlendState = &colorBlendingInfo,
            .pDynamicState = &dynamicStateInfo,
            .layout = *pipelineLayout,
            .renderPass = nullptr,
        },
        {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &swapChainSurfaceFormat.format,
            .depthAttachmentFormat = depthFormat,
        },
    };

    graphicsPipeline =
        vk::raii::Pipeline(device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
    std::cout << "Graphics pipeline: created\n";
  }

  [[nodiscard]] vk::raii::ShaderModule createShaderModule(std::vector<char> const& code) const {
    vk::ShaderModuleCreateInfo createInfo{
        .codeSize = code.size(),
        .pCode = std::bit_cast<uint32_t const*>(code.data()),
    };
    return {device, createInfo};
  }

  void createCommandPool() {
    vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = graphicsQueueFamilyIndex,
    };
    commandPool = vk::raii::CommandPool(device, poolInfo);
  }

  [[nodiscard]] vk::raii::CommandBuffer beginSingleTimeCommands() const {
    vk::raii::CommandBuffer cmd = std::move(vk::raii::CommandBuffers(device,
                                                                     vk::CommandBufferAllocateInfo{
                                                                         .commandPool = *commandPool,
                                                                         .level = vk::CommandBufferLevel::ePrimary,
                                                                         .commandBufferCount = 1,
                                                                     })
                                                .front());
    cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    return cmd;
  }

  void endSingleTimeCommands(vk::raii::CommandBuffer cmd) const {
    cmd.end();
    vk::CommandBuffer cmdHandle = *cmd;
    graphicsQueue.submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &cmdHandle}, nullptr);
    graphicsQueue.waitIdle();
  }

  std::pair<vk::raii::Image, vk::raii::DeviceMemory> createImage(uint32_t width, uint32_t height, uint32_t numMipLevels,
                                                                 vk::SampleCountFlagBits numSamples, vk::Format format,
                                                                 vk::ImageTiling tiling, vk::ImageUsageFlags usage,
                                                                 vk::MemoryPropertyFlags properties) {
    vk::raii::Image image(device, vk::ImageCreateInfo{
                                      .imageType = vk::ImageType::e2D,
                                      .format = format,
                                      .extent = {width, height, 1},
                                      .mipLevels = numMipLevels,
                                      .arrayLayers = 1,
                                      .samples = numSamples,
                                      .tiling = tiling,
                                      .usage = usage,
                                      .sharingMode = vk::SharingMode::eExclusive,
                                      .initialLayout = vk::ImageLayout::eUndefined,
                                  });

    vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
    vk::raii::DeviceMemory imageMemory(
        device, vk::MemoryAllocateInfo{
                    .allocationSize = memRequirements.size,
                    .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties),
                });
    image.bindMemory(*imageMemory, 0);

    return {std::move(image), std::move(imageMemory)};
  }

  static void copyBufferToImage(vk::raii::CommandBuffer const& cmd, vk::raii::Buffer const& buffer,
                                vk::raii::Image const& image, uint32_t width, uint32_t height) {
    vk::BufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                             .mipLevel = 0,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1},
    };
    cmd.copyBufferToImage(*buffer, *image, vk::ImageLayout::eTransferDstOptimal, region);
  }

  void generateMipmaps(vk::raii::CommandBuffer const& cmd, vk::raii::Image const& image, vk::Format imageFormat,
                       int32_t texWidth, int32_t texHeight, uint32_t numMipLevels) {
    vk::FormatProperties formatProperties = physicalDevice.getFormatProperties(imageFormat);
    if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
      throw std::runtime_error("texture image format does not support linear blitting!");
    }

    vk::ImageMemoryBarrier2 barrier{
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = *image,
        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
    };

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < numMipLevels; i++) {
      barrier.subresourceRange.baseMipLevel = i - 1;
      barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
      barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
      barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
      barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
      barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
      barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
      cmd.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});

      vk::ImageBlit blit{
          .srcSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = i - 1, .layerCount = 1},
          .srcOffsets = std::array<vk::Offset3D, 2>{vk::Offset3D{0, 0, 0}, vk::Offset3D{mipWidth, mipHeight, 1}},
          .dstSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = i, .layerCount = 1},
          .dstOffsets =
              std::array<vk::Offset3D, 2>{vk::Offset3D{0, 0, 0}, vk::Offset3D{mipWidth > 1 ? mipWidth / 2 : 1,
                                                                              mipHeight > 1 ? mipHeight / 2 : 1, 1}},
      };
      cmd.blitImage(*image, vk::ImageLayout::eTransferSrcOptimal, *image, vk::ImageLayout::eTransferDstOptimal, blit,
                    vk::Filter::eLinear);

      barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
      barrier.srcAccessMask = vk::AccessFlagBits2::eTransferRead;
      barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
      barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
      barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
      barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
      cmd.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});

      if (mipWidth > 1) {
        mipWidth /= 2;
      }
      if (mipHeight > 1) {
        mipHeight /= 2;
      }
    }

    // Transition the last mip level (never used as blit source, still in TransferDst)
    barrier.subresourceRange.baseMipLevel = numMipLevels - 1;
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
    barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    cmd.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
  }

  void createTextureImage() {
    ktxTexture2* kTexture = nullptr;
    KTX_error_code result = ktxTexture2_CreateFromNamedFile(
        TEXTURE_PATH.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
        &kTexture);
    if (result != KTX_SUCCESS) {
      throw std::runtime_error("failed to load KTX2 texture: " + std::string(ktxErrorString(result)));
    }

    // Transcode supercompressed formats (e.g. BasisLZ/UASTC) to a GPU-native format.
    if (ktxTexture2_NeedsTranscoding(kTexture)) {
      result = ktxTexture2_TranscodeBasis(kTexture, KTX_TTF_BC7_RGBA, 0);
      if (result != KTX_SUCCESS) {
        ktxTexture_Destroy(ktxTexture(kTexture));
        throw std::runtime_error("failed to transcode KTX2 texture: " + std::string(ktxErrorString(result)));
      }
    }

    uint32_t texWidth = kTexture->baseWidth;
    uint32_t texHeight = kTexture->baseHeight;
    mipLevels = kTexture->numLevels;
    textureFormat = static_cast<vk::Format>(kTexture->vkFormat);

    // Determine total data size across all mip levels.
    vk::DeviceSize totalSize = ktxTexture_GetDataSizeUncompressed(ktxTexture(kTexture));

    auto [stagingBuffer, stagingBufferMemory] =
        createBuffer(totalSize, vk::BufferUsageFlagBits::eTransferSrc,
                     vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* mappedData = stagingBufferMemory.mapMemory(0, totalSize);
    memcpy(mappedData, ktxTexture_GetData(ktxTexture(kTexture)), static_cast<size_t>(totalSize));
    stagingBufferMemory.unmapMemory();

    // Build one copy region per mip level.
    std::vector<vk::BufferImageCopy> mipCopyRegions;
    mipCopyRegions.reserve(mipLevels);
    for (uint32_t level = 0; level < mipLevels; ++level) {
      ktx_size_t offset = 0;
      ktxTexture_GetImageOffset(ktxTexture(kTexture), level, 0, 0, &offset);
      mipCopyRegions.push_back(vk::BufferImageCopy{
          .bufferOffset = offset,
          .bufferRowLength = 0,
          .bufferImageHeight = 0,
          .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                               .mipLevel = level,
                               .baseArrayLayer = 0,
                               .layerCount = 1},
          .imageOffset = {0, 0, 0},
          .imageExtent = {std::max(1u, texWidth >> level), std::max(1u, texHeight >> level), 1},
      });
    }

    ktxTexture_Destroy(ktxTexture(kTexture));

    vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    std::tie(textureImage, textureImageMemory) =
        createImage(texWidth, texHeight, mipLevels, vk::SampleCountFlagBits::e1, textureFormat,
                    vk::ImageTiling::eOptimal, usage, vk::MemoryPropertyFlagBits::eDeviceLocal);

    vk::raii::CommandBuffer cmd = beginSingleTimeCommands();

    transitionImageLayout(cmd, *textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, {},
                          vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eTopOfPipe,
                          vk::PipelineStageFlagBits2::eTransfer, vk::ImageAspectFlagBits::eColor, mipLevels);

    cmd.copyBufferToImage(*stagingBuffer, *textureImage, vk::ImageLayout::eTransferDstOptimal, mipCopyRegions);

    transitionImageLayout(cmd, *textureImage, vk::ImageLayout::eTransferDstOptimal,
                          vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits2::eTransferWrite,
                          vk::AccessFlagBits2::eShaderRead, vk::PipelineStageFlagBits2::eTransfer,
                          vk::PipelineStageFlagBits2::eFragmentShader, vk::ImageAspectFlagBits::eColor, mipLevels);

    endSingleTimeCommands(std::move(cmd));

    std::cout << "Texture image: " << texWidth << "x" << texHeight << " (" << mipLevels << " mip levels, format "
              << vk::to_string(textureFormat) << ") loaded\n";
  }

  [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const {
    vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
      if ((typeFilter & (1u << i)) && (memProperties.memoryTypes.at(i).propertyFlags & properties) == properties) {
        return i;
      }
    }
    throw std::runtime_error("failed to find suitable memory type!");
  }

  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage,
                                                                   vk::MemoryPropertyFlags properties) {
    vk::raii::Buffer buffer(device, vk::BufferCreateInfo{
                                        .size = size,
                                        .usage = usage,
                                        .sharingMode = vk::SharingMode::eExclusive,
                                    });

    vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
    vk::raii::DeviceMemory memory(device,
                                  vk::MemoryAllocateInfo{
                                      .allocationSize = memRequirements.size,
                                      .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties),
                                  });
    buffer.bindMemory(*memory, 0);
    return {std::move(buffer), std::move(memory)};
  }

  void copyBuffer(vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer, vk::DeviceSize size) {
    vk::raii::CommandBuffer cmd = std::move(device
                                                .allocateCommandBuffers(vk::CommandBufferAllocateInfo{
                                                    .commandPool = *commandPool,
                                                    .level = vk::CommandBufferLevel::ePrimary,
                                                    .commandBufferCount = 1,
                                                })
                                                .front());

    cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    cmd.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy{.srcOffset = 0, .dstOffset = 0, .size = size});
    cmd.end();

    vk::CommandBuffer cmdHandle = *cmd;
    graphicsQueue.submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &cmdHandle}, nullptr);
    graphicsQueue.waitIdle();
  }

  void loadModel() {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string warn;
    std::string err;

    bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, MODEL_PATH);
    if (!warn.empty()) {
      std::cerr << "glTF warning: " << warn << "\n";
    }
    if (!ret) {
      throw std::runtime_error("failed to load glTF model: " + err);
    }

    std::unordered_map<Vertex, uint32_t> uniqueVertices;

    for (auto const& mesh : model.meshes) {
      for (auto const& primitive : mesh.primitives) {
        // --- Position attribute ---
        auto const& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
        auto const& posView = model.bufferViews[posAccessor.bufferView];
        auto const* posData = reinterpret_cast<float const*>(
            model.buffers[posView.buffer].data.data() + posView.byteOffset + posAccessor.byteOffset);
        size_t posStride = posView.byteStride ? posView.byteStride / sizeof(float) : 3;

        // --- Texture coordinate attribute ---
        float const* uvData = nullptr;
        size_t uvStride = 2;
        if (primitive.attributes.count("TEXCOORD_0")) {
          auto const& uvAccessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
          auto const& uvView = model.bufferViews[uvAccessor.bufferView];
          uvData = reinterpret_cast<float const*>(model.buffers[uvView.buffer].data.data() + uvView.byteOffset +
                                                  uvAccessor.byteOffset);
          uvStride = uvView.byteStride ? uvView.byteStride / sizeof(float) : 2;
        }

        // Build a local remap from glTF vertex index → global deduplicated index.
        size_t vertexCount = posAccessor.count;
        std::vector<uint32_t> localRemap(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
          Vertex vertex{};
          vertex.pos = {posData[i * posStride], posData[i * posStride + 1], posData[i * posStride + 2]};
          vertex.color = {1.0f, 1.0f, 1.0f};
          if (uvData) {
            // glTF UV origin is top-left (OpenGL convention); flip V for Vulkan.
            vertex.texCoord = {uvData[i * uvStride], 1.0f - uvData[i * uvStride + 1]};
          }
          auto [it, inserted] = uniqueVertices.insert({vertex, static_cast<uint32_t>(vertices.size())});
          if (inserted) {
            vertices.push_back(vertex);
          }
          localRemap[i] = it->second;
        }

        // --- Index data: read raw glTF indices and remap through localRemap ---
        auto const& idxAccessor = model.accessors[primitive.indices];
        auto const& idxView = model.bufferViews[idxAccessor.bufferView];
        auto const* rawIdx = model.buffers[idxView.buffer].data.data() + idxView.byteOffset + idxAccessor.byteOffset;
        for (size_t i = 0; i < idxAccessor.count; ++i) {
          uint32_t rawIndex = 0;
          if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            rawIndex = reinterpret_cast<uint16_t const*>(rawIdx)[i];
          } else if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            rawIndex = reinterpret_cast<uint32_t const*>(rawIdx)[i];
          } else if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            rawIndex = reinterpret_cast<uint8_t const*>(rawIdx)[i];
          }
          indices.push_back(localRemap[rawIndex]);
        }
      }
    }

    std::cout << "Model loaded: " << vertices.size() << " unique vertices, " << indices.size() << " indices\n";
  }

  void createVertexBuffer() {
    vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    auto [stagingBuffer, stagingMemory] =
        createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                     vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* data = stagingMemory.mapMemory(0, bufferSize);
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    stagingMemory.unmapMemory();

    std::tie(vertexBuffer, vertexBufferMemory) =
        createBuffer(bufferSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                     vk::MemoryPropertyFlagBits::eDeviceLocal);

    copyBuffer(stagingBuffer, vertexBuffer, bufferSize);
  }

  void createIndexBuffer() {
    vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    auto [stagingBuffer, stagingMemory] =
        createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                     vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* data = stagingMemory.mapMemory(0, bufferSize);
    memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
    stagingMemory.unmapMemory();

    std::tie(indexBuffer, indexBufferMemory) =
        createBuffer(bufferSize, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                     vk::MemoryPropertyFlagBits::eDeviceLocal);

    copyBuffer(stagingBuffer, indexBuffer, bufferSize);
  }

  void createDescriptorSetLayout() {
    std::array<vk::DescriptorSetLayoutBinding, 2> bindings{{
        {.binding = 0,
         .descriptorType = vk::DescriptorType::eUniformBuffer,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eVertex},
        {.binding = 1,
         .descriptorType = vk::DescriptorType::eCombinedImageSampler,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eFragment},
    }};
    descriptorSetLayout =
        vk::raii::DescriptorSetLayout(device, vk::DescriptorSetLayoutCreateInfo{
                                                  .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                  .pBindings = bindings.data(),
                                              });
  }

  void createUniformBuffers() {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      auto [buffer, memory] =
          createBuffer(sizeof(UniformBufferObject), vk::BufferUsageFlagBits::eUniformBuffer,
                       vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
      uniformBuffersMapped.push_back(memory.mapMemory(0, sizeof(UniformBufferObject)));
      uniformBuffers.push_back(std::move(buffer));
      uniformBuffersMemory.push_back(std::move(memory));
    }
  }

  void updateUniformBuffer() {
    static auto startTime = std::chrono::high_resolution_clock::now();
    static auto lastTime = startTime;
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float>(currentTime - startTime).count();
    float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count() * 1000.0f;
    lastTime = currentTime;

    UniformBufferObject ubo{
        .model = glm::rotate(glm::mat4(1.0f), time * glm::radians(15.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        .view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        .proj = glm::perspective(glm::radians(45.0f),
                                 static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height),
                                 0.1f, 10.0f),
    };
    ubo.proj[1][1] *= -1;  // GLM uses OpenGL clip space (Y up); Vulkan is Y down.
    memcpy(uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));

    ComputeUBO cubo{.deltaTime = deltaTime};
    memcpy(computeUniformBuffersMapped[frameIndex], &cubo, sizeof(cubo));
  }

  void createDescriptorPool() {
    std::array<vk::DescriptorPoolSize, 2> poolSizes{{
        {.type = vk::DescriptorType::eUniformBuffer, .descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT)},
        {.type = vk::DescriptorType::eCombinedImageSampler,
         .descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT)},
    }};
    descriptorPool = vk::raii::DescriptorPool(device, vk::DescriptorPoolCreateInfo{
                                                          .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                                          .maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
                                                          .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                                                          .pPoolSizes = poolSizes.data(),
                                                      });
  }

  void createDescriptorSets() {
    std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout);
    descriptorSets = vk::raii::DescriptorSets(device, vk::DescriptorSetAllocateInfo{
                                                          .descriptorPool = *descriptorPool,
                                                          .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
                                                          .pSetLayouts = layouts.data(),
                                                      });

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      vk::DescriptorBufferInfo bufferInfo{
          .buffer = *uniformBuffers[i],
          .offset = 0,
          .range = sizeof(UniformBufferObject),
      };
      vk::DescriptorImageInfo imageInfo{
          .sampler = *textureSampler,
          .imageView = *textureImageView,
          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
      };
      std::array<vk::WriteDescriptorSet, 2> descriptorWrites{{
          {.dstSet = *descriptorSets[i],
           .dstBinding = 0,
           .dstArrayElement = 0,
           .descriptorCount = 1,
           .descriptorType = vk::DescriptorType::eUniformBuffer,
           .pBufferInfo = &bufferInfo},
          {.dstSet = *descriptorSets[i],
           .dstBinding = 1,
           .dstArrayElement = 0,
           .descriptorCount = 1,
           .descriptorType = vk::DescriptorType::eCombinedImageSampler,
           .pImageInfo = &imageInfo},
      }};
      device.updateDescriptorSets(descriptorWrites, {});
    }
  }

  void createCommandBuffers() {
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = *commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
    };
    commandBuffers = vk::raii::CommandBuffers(device, allocInfo);
  }

  void createComputeCommandBuffers() {
    computeCommandBuffers = vk::raii::CommandBuffers(device, vk::CommandBufferAllocateInfo{
                                                                 .commandPool = *commandPool,
                                                                 .level = vk::CommandBufferLevel::ePrimary,
                                                                 .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
                                                             });
  }

  void createShaderStorageBuffers() {
    std::default_random_engine rndEngine(static_cast<unsigned>(time(nullptr)));
    std::uniform_real_distribution<float> rndDist(0.0f, 1.0f);

    std::vector<Particle> particles(PARTICLE_COUNT);
    for (auto& particle : particles) {
      float radius = 0.25f * std::sqrt(rndDist(rndEngine));
      float theta = rndDist(rndEngine) * 2.0f * std::numbers::pi_v<float>;
      float posX = radius * std::cos(theta) * static_cast<float>(HEIGHT) / static_cast<float>(WIDTH);
      float posY = radius * std::sin(theta);
      particle.position = glm::vec2(posX, posY);
      particle.velocity = glm::normalize(glm::vec2(posX, posY)) * 0.00025f;
      particle.color = glm::vec4(rndDist(rndEngine), rndDist(rndEngine), rndDist(rndEngine), 1.0f);
    }

    vk::DeviceSize bufferSize = sizeof(Particle) * PARTICLE_COUNT;

    auto [stagingBuffer, stagingMemory] =
        createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                     vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* data = stagingMemory.mapMemory(0, bufferSize);
    memcpy(data, particles.data(), static_cast<size_t>(bufferSize));
    stagingMemory.unmapMemory();

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      auto [ssbo, ssboMemory] =
          createBuffer(bufferSize,
                       vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer |
                           vk::BufferUsageFlagBits::eTransferDst,
                       vk::MemoryPropertyFlagBits::eDeviceLocal);
      copyBuffer(stagingBuffer, ssbo, bufferSize);
      shaderStorageBuffers.push_back(std::move(ssbo));
      shaderStorageBuffersMemory.push_back(std::move(ssboMemory));
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      auto [uboBuf, uboMemory] =
          createBuffer(sizeof(ComputeUBO), vk::BufferUsageFlagBits::eUniformBuffer,
                       vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
      computeUniformBuffersMapped.push_back(uboMemory.mapMemory(0, sizeof(ComputeUBO)));
      computeUniformBuffers.push_back(std::move(uboBuf));
      computeUniformBuffersMemory.push_back(std::move(uboMemory));
    }

    std::cout << "Shader storage buffers: " << PARTICLE_COUNT << " particles, " << MAX_FRAMES_IN_FLIGHT
              << " SSBO pairs\n";
  }

  void createComputeDescriptorSetLayout() {
    std::array<vk::DescriptorSetLayoutBinding, 3> bindings{{
        {.binding = 0,
         .descriptorType = vk::DescriptorType::eUniformBuffer,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eCompute},
        {.binding = 1,
         .descriptorType = vk::DescriptorType::eStorageBuffer,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eCompute},
        {.binding = 2,
         .descriptorType = vk::DescriptorType::eStorageBuffer,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eCompute},
    }};
    computeDescriptorSetLayout =
        vk::raii::DescriptorSetLayout(device, vk::DescriptorSetLayoutCreateInfo{
                                                  .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                  .pBindings = bindings.data(),
                                              });
  }

  void createComputePipeline() {
    auto compCode = readFile("shaders/compiled/particle.comp.spv");
    vk::raii::ShaderModule compModule = createShaderModule(compCode);

    vk::PipelineShaderStageCreateInfo compStageInfo{
        .stage = vk::ShaderStageFlagBits::eCompute,
        .module = *compModule,
        .pName = "main",
    };

    vk::DescriptorSetLayout dslHandle = *computeDescriptorSetLayout;
    computePipelineLayout = vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo{
                                                                 .setLayoutCount = 1,
                                                                 .pSetLayouts = &dslHandle,
                                                             });

    computePipeline = vk::raii::Pipeline(
        device, nullptr, vk::ComputePipelineCreateInfo{.stage = compStageInfo, .layout = *computePipelineLayout});
    std::cout << "Compute pipeline: created\n";
  }

  void createParticlePipeline() {
    auto vertCode = readFile("shaders/compiled/particle.vert.spv");
    auto fragCode = readFile("shaders/compiled/particle.frag.spv");

    vk::raii::ShaderModule vertModule = createShaderModule(vertCode);
    vk::raii::ShaderModule fragModule = createShaderModule(fragCode);

    std::array shaderStages = {
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex, .module = *vertModule, .pName = "main"},
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment, .module = *fragModule, .pName = "main"},
    };

    std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };

    auto bindingDesc = Particle::getBindingDescription();
    auto attrDescs = Particle::getAttributeDescriptions();
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDesc,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size()),
        .pVertexAttributeDescriptions = attrDescs.data(),
    };

    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
        .topology = vk::PrimitiveTopology::ePointList,
        .primitiveRestartEnable = vk::False,
    };

    vk::PipelineViewportStateCreateInfo viewportStateInfo{.viewportCount = 1, .scissorCount = 1};

    vk::PipelineRasterizationStateCreateInfo rasterizerInfo{
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0f,
    };

    vk::PipelineMultisampleStateCreateInfo multisamplingInfo{
        .rasterizationSamples = msaaSamples,
        .sampleShadingEnable = vk::False,
    };

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = vk::True,
        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eZero,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    vk::PipelineColorBlendStateCreateInfo colorBlendingInfo{
        .logicOpEnable = vk::False,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
    };

    vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{
        .depthTestEnable = vk::False,
        .depthWriteEnable = vk::False,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False,
    };

    particlePipelineLayout = vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo{.setLayoutCount = 0});

    vk::Format depthFormat = findDepthFormat();
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain = {
        {
            .stageCount = static_cast<uint32_t>(shaderStages.size()),
            .pStages = shaderStages.data(),
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssemblyInfo,
            .pViewportState = &viewportStateInfo,
            .pRasterizationState = &rasterizerInfo,
            .pMultisampleState = &multisamplingInfo,
            .pDepthStencilState = &depthStencilInfo,
            .pColorBlendState = &colorBlendingInfo,
            .pDynamicState = &dynamicStateInfo,
            .layout = *particlePipelineLayout,
        },
        {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &swapChainSurfaceFormat.format,
            .depthAttachmentFormat = depthFormat,
        },
    };

    particlePipeline =
        vk::raii::Pipeline(device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
    std::cout << "Particle pipeline: created\n";
  }

  void createComputeDescriptorSets() {
    std::array poolSizes = {
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformBuffer,
                               .descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT)},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageBuffer,
                               .descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) * 2},
    };
    computeDescriptorPool =
        vk::raii::DescriptorPool(device, vk::DescriptorPoolCreateInfo{
                                             .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                             .maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
                                             .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                                             .pPoolSizes = poolSizes.data(),
                                         });

    std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *computeDescriptorSetLayout);
    computeDescriptorSets =
        vk::raii::DescriptorSets(device, vk::DescriptorSetAllocateInfo{
                                             .descriptorPool = *computeDescriptorPool,
                                             .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
                                             .pSetLayouts = layouts.data(),
                                         });

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      vk::DescriptorBufferInfo uboInfo{
          .buffer = *computeUniformBuffers[i],
          .offset = 0,
          .range = sizeof(ComputeUBO),
      };
      int prevFrame = (i - 1 + MAX_FRAMES_IN_FLIGHT) % MAX_FRAMES_IN_FLIGHT;
      vk::DescriptorBufferInfo ssboLastInfo{
          .buffer = *shaderStorageBuffers[prevFrame],
          .offset = 0,
          .range = sizeof(Particle) * PARTICLE_COUNT,
      };
      vk::DescriptorBufferInfo ssboCurrInfo{
          .buffer = *shaderStorageBuffers[i],
          .offset = 0,
          .range = sizeof(Particle) * PARTICLE_COUNT,
      };
      std::array<vk::WriteDescriptorSet, 3> writes{{
          {.dstSet = *computeDescriptorSets[i],
           .dstBinding = 0,
           .dstArrayElement = 0,
           .descriptorCount = 1,
           .descriptorType = vk::DescriptorType::eUniformBuffer,
           .pBufferInfo = &uboInfo},
          {.dstSet = *computeDescriptorSets[i],
           .dstBinding = 1,
           .dstArrayElement = 0,
           .descriptorCount = 1,
           .descriptorType = vk::DescriptorType::eStorageBuffer,
           .pBufferInfo = &ssboLastInfo},
          {.dstSet = *computeDescriptorSets[i],
           .dstBinding = 2,
           .dstArrayElement = 0,
           .descriptorCount = 1,
           .descriptorType = vk::DescriptorType::eStorageBuffer,
           .pBufferInfo = &ssboCurrInfo},
      }};
      device.updateDescriptorSets(writes, {});
    }
  }

  void recordComputeCommandBuffer(uint32_t frameIdx) {
    auto const& cmd = computeCommandBuffers[frameIdx];
    cmd.begin({});
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *computePipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *computePipelineLayout, 0, *computeDescriptorSets[frameIdx],
                           {});
    cmd.dispatch(PARTICLE_COUNT / 256, 1, 1);
    cmd.end();
  }

  static void transitionImageLayout(vk::raii::CommandBuffer const& cmd, vk::Image image, vk::ImageLayout oldLayout,
                                    vk::ImageLayout newLayout, vk::AccessFlags2 srcAccess, vk::AccessFlags2 dstAccess,
                                    vk::PipelineStageFlags2 srcStage, vk::PipelineStageFlags2 dstStage,
                                    vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor,
                                    uint32_t numMipLevels = 1) {
    vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image,
        .subresourceRange = {aspectFlags, 0, numMipLevels, 0, 1},
    };
    cmd.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    });
  }

  void recordCommandBuffer(uint32_t imageIndex) {
    auto const& cmd = commandBuffers[frameIndex];
    cmd.begin({});

    transitionImageLayout(cmd, swapChainImages[imageIndex], vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eColorAttachmentOptimal, {}, vk::AccessFlagBits2::eColorAttachmentWrite,
                          vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                          vk::PipelineStageFlagBits2::eColorAttachmentOutput);

    transitionImageLayout(cmd, *colorImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal, {},
                          vk::AccessFlagBits2::eColorAttachmentWrite,
                          vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                          vk::PipelineStageFlagBits2::eColorAttachmentOutput);

    transitionImageLayout(
        cmd, *depthImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal,
        vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
        vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
        vk::ImageAspectFlagBits::eDepth);

    vk::ClearValue clearColor = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f};
    vk::RenderingAttachmentInfo colorAttachmentInfo{
        .imageView = *colorImageView,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .resolveMode = vk::ResolveModeFlagBits::eAverage,
        .resolveImageView = *swapChainImageViews[imageIndex],
        .resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = clearColor,
    };
    vk::ClearValue clearDepth = vk::ClearDepthStencilValue{1.0f, 0};
    vk::RenderingAttachmentInfo depthAttachmentInfo{
        .imageView = *depthImageView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = clearDepth,
    };
    vk::RenderingInfo renderingInfo{
        .renderArea = {.offset = {0, 0}, .extent = swapChainExtent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentInfo,
        .pDepthAttachment = &depthAttachmentInfo,
    };

    cmd.beginRendering(renderingInfo);

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);
    cmd.bindVertexBuffers(0, *vertexBuffer, {vk::DeviceSize{0}});
    cmd.bindIndexBuffer(*indexBuffer, 0, vk::IndexType::eUint32);

    vk::Viewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(swapChainExtent.width),
        .height = static_cast<float>(swapChainExtent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, vk::Rect2D{.offset = {0, 0}, .extent = swapChainExtent});

    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, *descriptorSets[frameIndex], {});

    cmd.drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

    // Draw particles on top of the scene
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *particlePipeline);
    cmd.bindVertexBuffers(0, *shaderStorageBuffers[frameIndex], {vk::DeviceSize{0}});
    cmd.draw(PARTICLE_COUNT, 1, 0, 0);

    cmd.endRendering();

    transitionImageLayout(cmd, swapChainImages[imageIndex], vk::ImageLayout::eColorAttachmentOptimal,
                          vk::ImageLayout::ePresentSrcKHR, vk::AccessFlagBits2::eColorAttachmentWrite, {},
                          vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                          vk::PipelineStageFlagBits2::eBottomOfPipe);

    cmd.end();
  }

  void createSyncObjects() {
    for (size_t i = 0; i < swapChainImages.size(); i++) {
      renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo{});
    }
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      presentCompleteSemaphores.emplace_back(device, vk::SemaphoreCreateInfo{});
      inFlightFences.emplace_back(device, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
      computeFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo{});
      computeInFlightFences.emplace_back(device, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
    }
  }

  void drawFrame() {
    // --- Compute pass ---
    std::ignore =
        device.waitForFences(*computeInFlightFences[frameIndex], vk::True, std::numeric_limits<uint64_t>::max());
    device.resetFences(*computeInFlightFences[frameIndex]);

    updateUniformBuffer();

    computeCommandBuffers[frameIndex].reset();
    recordComputeCommandBuffer(frameIndex);

    vk::CommandBuffer computeCmdBuf = *computeCommandBuffers[frameIndex];
    vk::SubmitInfo computeSubmitInfo{
        .commandBufferCount = 1,
        .pCommandBuffers = &computeCmdBuf,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &*computeFinishedSemaphores[frameIndex],
    };
    computeQueue.submit(computeSubmitInfo, *computeInFlightFences[frameIndex]);

    // --- Graphics pass ---
    std::ignore = device.waitForFences(*inFlightFences[frameIndex], vk::True, std::numeric_limits<uint64_t>::max());

    auto [acquireResult, imageIndex] = swapChain.acquireNextImage(std::numeric_limits<uint64_t>::max(),
                                                                  *presentCompleteSemaphores[frameIndex], nullptr);

    if (acquireResult == vk::Result::eErrorOutOfDateKHR) {
      recreateSwapChain();
      return;
    }
    if (acquireResult != vk::Result::eSuccess && acquireResult != vk::Result::eSuboptimalKHR) {
      throw std::runtime_error("failed to acquire swap chain image!");
    }

    // Reset fence only after confirming we will submit — avoids unsignalled fence deadlock.
    device.resetFences(*inFlightFences[frameIndex]);

    commandBuffers[frameIndex].reset();
    recordCommandBuffer(imageIndex);

    std::array waitSemaphores = {*presentCompleteSemaphores[frameIndex], *computeFinishedSemaphores[frameIndex]};
    std::array<vk::PipelineStageFlags, 2> waitStages = {vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                                        vk::PipelineStageFlagBits::eVertexInput};
    vk::CommandBuffer cmdBuf = *commandBuffers[frameIndex];
    vk::SubmitInfo submitInfo{
        .waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size()),
        .pWaitSemaphores = waitSemaphores.data(),
        .pWaitDstStageMask = waitStages.data(),
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdBuf,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &*renderFinishedSemaphores[imageIndex],
    };
    graphicsQueue.submit(submitInfo, *inFlightFences[frameIndex]);

    vk::SwapchainKHR swapChainHandle = *swapChain;
    vk::PresentInfoKHR presentInfo{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*renderFinishedSemaphores[imageIndex],
        .swapchainCount = 1,
        .pSwapchains = &swapChainHandle,
        .pImageIndices = &imageIndex,
    };
    vk::Result presentResult = graphicsQueue.presentKHR(presentInfo);
    if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR ||
        framebufferResized) {
      framebufferResized = false;
      recreateSwapChain();
    } else if (presentResult != vk::Result::eSuccess) {
      throw std::runtime_error("failed to present swap chain image!");
    }

    frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
  }

  static std::vector<char> readFile(std::string const& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
      throw std::runtime_error("failed to open file: " + filename);
    }
    std::vector<char> buffer(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    return buffer;
  }

  void createLogicalDevice() {
    // Find the index of the first queue family that supports graphics
    std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

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

    // Enable required features via a pNext chain
    vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
                       vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
        featureChain = {
            {.features = {.sampleRateShading = true, .samplerAnisotropy = true}},  // vk::PhysicalDeviceFeatures2
            {.shaderDrawParameters = true},                                        // vk::PhysicalDeviceVulkan11Features
            {.synchronization2 = true, .dynamicRendering = true},                  // vk::PhysicalDeviceVulkan13Features
            {.extendedDynamicState = true}  // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
        };

    float queuePriority = 0.5f;
    vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
        .queueFamilyIndex = graphicsIndex, .queueCount = 1, .pQueuePriorities = &queuePriority};
    vk::DeviceCreateInfo deviceCreateInfo{
        .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &deviceQueueCreateInfo,
        .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size()),
        .ppEnabledExtensionNames = requiredDeviceExtension.data()};

    device = vk::raii::Device(physicalDevice, deviceCreateInfo);
    graphicsQueue = vk::raii::Queue(device, graphicsIndex, 0);
    computeQueue = vk::raii::Queue(device, graphicsIndex, 0);
    graphicsQueueFamilyIndex = graphicsIndex;

    std::cout << "Queues:\n";
    std::cout << "  graphics + present + compute (family " << graphicsIndex << ")\n";
  }

  static void framebufferResizeCallback(GLFWwindow* window, int /*width*/, int /*height*/) {
    auto* app = static_cast<HelloTriangleApplication*>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
  }

  static std::vector<const char*> getRequiredInstanceExtensions() {
    uint32_t glfwExtensionCount = 0;
    auto* glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    if (ENABLE_VALIDATION_LAYERS) {
      extensions.push_back(vk::EXTDebugUtilsExtensionName);
    }

    return extensions;
  }

  static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                                        vk::DebugUtilsMessageTypeFlagsEXT type,
                                                        const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                        void* /*unused*/) {
    if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError ||
        severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
      std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << "\n";
    }

    return vk::False;
  }
};

int main() {
  try {
    HelloTriangleApplication app;
    app.run();
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
