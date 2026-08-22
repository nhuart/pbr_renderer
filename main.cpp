#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#define GLFW_INCLUDE_VULKAN  // REQUIRED only for GLFW CreateWindowSurface.
#include <GLFW/glfw3.h>

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;

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

  vk::raii::SurfaceKHR surface = nullptr;

  vk::raii::SwapchainKHR swapChain = nullptr;
  std::vector<vk::Image> swapChainImages;
  vk::SurfaceFormatKHR swapChainSurfaceFormat;
  vk::Extent2D swapChainExtent;
  std::vector<vk::raii::ImageView> swapChainImageViews;

  vk::raii::PipelineLayout pipelineLayout = nullptr;
  vk::raii::Pipeline graphicsPipeline = nullptr;

  std::vector<const char*> requiredDeviceExtension = {vk::KHRSwapchainExtensionName};

  void initWindow() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
  }

  void initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createGraphicsPipeline();
  }

  void mainLoop() {
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();
    }
  }

  void cleanup() {
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
        features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
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
    std::cout << "Selected GPU: " << physicalDevice.getProperties().deviceName << "\n";
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

  void createImageViews() {
    vk::ImageViewCreateInfo createInfo{
        .viewType = vk::ImageViewType::e2D,
        .format = swapChainSurfaceFormat.format,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };

    for (auto& image : swapChainImages) {
      createInfo.image = image;
      swapChainImageViews.emplace_back(device, createInfo);
    }

    std::cout << "Image views: " << swapChainImageViews.size() << " created\n";
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

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 0,
        .vertexAttributeDescriptionCount = 0,
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
        .frontFace = vk::FrontFace::eClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0f,
    };

    vk::PipelineMultisampleStateCreateInfo multisamplingInfo{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False,
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

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
        .setLayoutCount = 0,
        .pushConstantRangeCount = 0,
    };
    pipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain = {
        {
            .stageCount = static_cast<uint32_t>(shaderStages.size()),
            .pStages = shaderStages.data(),
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssemblyInfo,
            .pViewportState = &viewportStateInfo,
            .pRasterizationState = &rasterizerInfo,
            .pMultisampleState = &multisamplingInfo,
            .pColorBlendState = &colorBlendingInfo,
            .pDynamicState = &dynamicStateInfo,
            .layout = *pipelineLayout,
            .renderPass = nullptr,
        },
        {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &swapChainSurfaceFormat.format,
        },
    };

    graphicsPipeline =
        vk::raii::Pipeline(device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
    std::cout << "Graphics pipeline: created\n";
  }

  [[nodiscard]] vk::raii::ShaderModule createShaderModule(std::vector<char> const& code) const {
    vk::ShaderModuleCreateInfo createInfo{
        .codeSize = code.size(),
        .pCode = reinterpret_cast<uint32_t const*>(code.data()),
    };
    return vk::raii::ShaderModule(device, createInfo);
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
            {},                              // vk::PhysicalDeviceFeatures2
            {.shaderDrawParameters = true},  // vk::PhysicalDeviceVulkan11Features
            {.dynamicRendering = true},      // vk::PhysicalDeviceVulkan13Features
            {.extendedDynamicState = true}   // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
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

    std::cout << "Queues:\n";
    std::cout << "  graphics + present (family " << graphicsIndex << ") — draw calls, rendering, and presentation\n";
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
