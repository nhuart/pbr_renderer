#include <cstdlib>
#include <iostream>
#include <stdexcept>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL

#include "src/core/application.hpp"

void Renderer::run() {
    scene = loadScene(scenePath);
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
}

void Renderer::initWindow() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}

void Renderer::initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createDescriptorSetLayout();
    if (scene.particles) createComputeDescriptorSetLayout();
    createGraphicsPipeline();
    if (scene.particles) createParticlePipeline();
    if (scene.particles) createComputePipeline();
    createCommandPool();
    createColorResources();
    createDepthResources();
    createTextureImage();
    createTextureImageView();
    createTextureSampler();
    loadModel();
    createVertexBuffer();
    createIndexBuffer();
    setupGameObjects();
    createUniformBuffers();
    if (scene.particles) createShaderStorageBuffers();
    createDescriptorPool();
    createDescriptorSets();
    if (scene.particles) createComputeDescriptorSets();
    createCommandBuffers();
    if (scene.particles) createComputeCommandBuffers();
    createSyncObjects();
}

void Renderer::mainLoop() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        drawFrame();
    }
    device.waitIdle();
}

void Renderer::cleanup() {
    cleanupSwapChain();
    glfwDestroyWindow(window);
    glfwTerminate();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: pbr_renderer <scene.json>\n";
        return EXIT_FAILURE;
    }
    try {
        Renderer app(argv[1]);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
