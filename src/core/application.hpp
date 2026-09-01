#pragma once

#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "scene/types.hpp"
#include "scene/scene_loader.hpp"

#include <unordered_map>

// Forward declarations — full definition only needed in model_loader.cpp
namespace tinygltf {
struct Model;
struct Primitive;
struct Accessor;
}

inline const std::vector<char const*> VALIDATION_LAYERS = {"VK_LAYER_KHRONOS_validation"};

#ifdef NDEBUG
constexpr bool ENABLE_VALIDATION_LAYERS = false;
#else
constexpr bool ENABLE_VALIDATION_LAYERS = true;
#endif

class Renderer {
 public:
  explicit Renderer(std::string scenePath) : scenePath(std::move(scenePath)) {}
  void run();

 private:
  // --- Scene ---
  std::string scenePath;
  Scene   scene;

  // --- Window ---
  GLFWwindow* window = nullptr;

  // --- Core Vulkan ---
  vk::raii::Context              context;
  vk::raii::Instance             instance       = nullptr;
  vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
  vk::raii::PhysicalDevice       physicalDevice = nullptr;
  vk::raii::Device               device         = nullptr;
  vk::raii::Queue                graphicsQueue  = nullptr;
  vk::raii::Queue                computeQueue   = nullptr;
  uint32_t                       graphicsQueueFamilyIndex = 0;
  vk::raii::SurfaceKHR           surface        = nullptr;

  // --- Swapchain ---
  vk::raii::SwapchainKHR              swapChain    = nullptr;
  std::vector<vk::Image>              swapChainImages;
  vk::SurfaceFormatKHR                swapChainSurfaceFormat;
  vk::Extent2D                        swapChainExtent;
  std::vector<vk::raii::ImageView>    swapChainImageViews;

  // --- Graphics pipeline ---
  vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
  vk::raii::PipelineLayout      pipelineLayout      = nullptr;
  vk::raii::DescriptorPool      descriptorPool      = nullptr;
  vk::raii::Pipeline            graphicsPipeline    = nullptr;

  // --- Particle pipeline ---
  vk::raii::Pipeline       particlePipeline       = nullptr;
  vk::raii::PipelineLayout particlePipelineLayout = nullptr;

  // --- Compute pipeline ---
  vk::raii::DescriptorSetLayout        computeDescriptorSetLayout = nullptr;
  vk::raii::PipelineLayout             computePipelineLayout      = nullptr;
  vk::raii::Pipeline                   computePipeline            = nullptr;
  vk::raii::DescriptorPool             computeDescriptorPool      = nullptr;
  std::vector<vk::raii::DescriptorSet> computeDescriptorSets;
  std::vector<vk::raii::Buffer>        shaderStorageBuffers;
  std::vector<vk::raii::DeviceMemory>  shaderStorageBuffersMemory;
  std::vector<vk::raii::Buffer>        computeUniformBuffers;
  std::vector<vk::raii::DeviceMemory>  computeUniformBuffersMemory;
  std::vector<void*>                   computeUniformBuffersMapped;

  // --- Scene / mesh data ---
  std::vector<GameObject> gameObjects;
  std::vector<Vertex>     vertices;
  std::vector<uint32_t>   indices;

  // --- Geometry buffers ---
  vk::raii::Buffer       vertexBuffer       = nullptr;
  vk::raii::DeviceMemory vertexBufferMemory = nullptr;
  vk::raii::Buffer       indexBuffer        = nullptr;
  vk::raii::DeviceMemory indexBufferMemory  = nullptr;

  // --- Commands ---
  vk::raii::CommandPool                commandPool = nullptr;
  std::vector<vk::raii::CommandBuffer> commandBuffers;
  std::vector<vk::raii::CommandBuffer> computeCommandBuffers;

  // --- MSAA / depth / color resolve ---
  vk::SampleCountFlagBits msaaSamples      = vk::SampleCountFlagBits::e1;
  vk::raii::Image         colorImage       = nullptr;
  vk::raii::DeviceMemory  colorImageMemory = nullptr;
  vk::raii::ImageView     colorImageView   = nullptr;
  vk::raii::Image         depthImage       = nullptr;
  vk::raii::DeviceMemory  depthImageMemory = nullptr;
  vk::raii::ImageView     depthImageView   = nullptr;

  // --- Texture ---
  uint32_t               mipLevels         = 0;
  vk::Format             textureFormat     = vk::Format::eR8G8B8A8Srgb;
  vk::raii::Image        textureImage      = nullptr;
  vk::raii::DeviceMemory textureImageMemory = nullptr;
  vk::raii::ImageView    textureImageView  = nullptr;
  vk::raii::Sampler      textureSampler    = nullptr;

  // --- Sync ---
  std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
  std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
  std::vector<vk::raii::Fence>     inFlightFences;
  std::vector<vk::raii::Semaphore> computeFinishedSemaphores;
  std::vector<vk::raii::Fence>     computeInFlightFences;

  // --- Frame state ---
  uint32_t frameIndex        = 0;
  bool     framebufferResized = false;

  std::vector<const char*> requiredDeviceExtension = {vk::KHRSwapchainExtensionName};

  // --- Lifecycle ---
  void initWindow();
  void initVulkan();
  void mainLoop();
  void cleanup();

  // --- Instance / debug (src/core/instance.cpp) ---
  void createInstance();
  void setupDebugMessenger();
  static std::vector<const char*> getRequiredInstanceExtensions();
  static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
      vk::DebugUtilsMessageSeverityFlagBitsEXT,
      vk::DebugUtilsMessageTypeFlagsEXT,
      const vk::DebugUtilsMessengerCallbackDataEXT*,
      void*);

  // --- Device (src/core/device.cpp) ---
  void pickPhysicalDevice();
  void createLogicalDevice();
  bool isDeviceSuitable(vk::raii::PhysicalDevice const& dev);
  [[nodiscard]] vk::SampleCountFlagBits getMaxUsableSampleCount() const;

  // --- Swapchain (src/core/swapchain.cpp) ---
  void createSurface();
  void createSwapChain();
  void createImageViews();
  void cleanupSwapChain();
  void recreateSwapChain();
  static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const&);
  static vk::PresentModeKHR   chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const&);
  vk::Extent2D                chooseSwapExtent(vk::SurfaceCapabilitiesKHR const&);
  static uint32_t             chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const&);
  static void framebufferResizeCallback(GLFWwindow*, int, int);

  // --- Resources: buffer (src/resources/buffer.cpp) ---
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> createBuffer(vk::DeviceSize, vk::BufferUsageFlags, vk::MemoryPropertyFlags);
  void copyBuffer(vk::raii::Buffer& src, vk::raii::Buffer& dst, vk::DeviceSize);
  [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags) const;
  void createVertexBuffer();
  void createIndexBuffer();
  void createUniformBuffers();
  void createShaderStorageBuffers();
  void updateUniformBuffer();

  // --- Resources: image (src/resources/image.cpp) ---
  std::pair<vk::raii::Image, vk::raii::DeviceMemory> createImage(uint32_t w, uint32_t h, uint32_t mipLevels,
      vk::SampleCountFlagBits, vk::Format, vk::ImageTiling, vk::ImageUsageFlags, vk::MemoryPropertyFlags);
  [[nodiscard]] vk::raii::ImageView createImageView(vk::Image, vk::Format,
      vk::ImageAspectFlags = vk::ImageAspectFlagBits::eColor, uint32_t mipLevels = 1) const;
  [[nodiscard]] vk::Format findSupportedFormat(std::vector<vk::Format> const&, vk::ImageTiling, vk::FormatFeatureFlags) const;
  [[nodiscard]] vk::Format findDepthFormat() const;
  void createColorResources();
  void createDepthResources();
  static void copyBufferToImage(vk::raii::CommandBuffer const&, vk::raii::Buffer const&,
                                vk::raii::Image const&, uint32_t w, uint32_t h);
  void generateMipmaps(vk::raii::CommandBuffer const&, vk::raii::Image const&, vk::Format,
                       int32_t texWidth, int32_t texHeight, uint32_t mipLevels);
  static void transitionImageLayout(vk::raii::CommandBuffer const&, vk::Image,
      vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
      vk::AccessFlags2 srcAccess, vk::AccessFlags2 dstAccess,
      vk::PipelineStageFlags2 srcStage, vk::PipelineStageFlags2 dstStage,
      vk::ImageAspectFlags = vk::ImageAspectFlagBits::eColor, uint32_t mipLevels = 1);

  // --- Resources: texture (src/resources/texture.cpp) ---
  void createTextureImage();
  void createTextureImageView();
  void createTextureSampler();

  // --- Scene (src/scene/model_loader.cpp) ---
  void loadModel();
  void loadPrimitive(tinygltf::Model const&, tinygltf::Primitive const&,
                     std::unordered_map<Vertex, uint32_t>&);
  static uint8_t const* accessorData(tinygltf::Model const&, tinygltf::Accessor const&);
  template <typename T>
  static T readAt(uint8_t const* ptr, size_t index) {
    T val{};
    memcpy(&val, ptr + index * sizeof(T), sizeof(T));
    return val;
  }

  // --- Renderer: pipeline (src/renderer/graphics_pipeline.cpp) ---
  void createGraphicsPipeline();
  void createParticlePipeline();
  [[nodiscard]] vk::raii::ShaderModule createShaderModule(std::vector<char> const&) const;
  static std::vector<char> readFile(std::string const&);

  // --- Renderer: descriptors (src/renderer/descriptors.cpp) ---
  void createDescriptorSetLayout();
  void createDescriptorPool();
  void createDescriptorSets();
  void setupGameObjects();

  // --- Renderer: commands (src/renderer/command_buffer.cpp) ---
  void createCommandPool();
  [[nodiscard]] vk::raii::CommandBuffer beginSingleTimeCommands() const;
  void endSingleTimeCommands(vk::raii::CommandBuffer) const;
  void createCommandBuffers();
  void createComputeCommandBuffers();
  void recordCommandBuffer(uint32_t imageIndex);
  void recordComputeCommandBuffer(uint32_t frameIdx);

  // --- Renderer: sync + frame loop (src/renderer/sync.cpp) ---
  void createSyncObjects();
  void drawFrame();

  // --- Compute (src/compute/compute_pipeline.cpp) ---
  void createComputeDescriptorSetLayout();
  void createComputePipeline();
  void createComputeDescriptorSets();
};
