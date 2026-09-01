#pragma once

#include <optional>
#include <string>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "scene/types.hpp"
#include "scene/scene_loader.hpp"
#include "core/context.hpp"
#include "core/swapchain.hpp"
#include "core/command_service.hpp"
#include "core/sync.hpp"
#include "renderer/texture_atlas.hpp"
#include "renderer/mesh_buffer.hpp"
#include "renderer/mesh_pipeline.hpp"
#include "renderer/particle_pipeline.hpp"

class Renderer {
public:
    explicit Renderer(std::string scenePath);
    void run();

private:
    std::string scenePath_;
    Scene       scene_;
    GLFWwindow* window_             = nullptr;
    uint32_t    frameIndex_         = 0;
    bool        framebufferResized_ = false;

    std::optional<VulkanContext>    ctx_;
    std::optional<Swapchain>        swapchain_;
    std::optional<CommandService>   cmds_;
    std::optional<SyncObjects>      sync_;
    std::optional<TextureAtlas>     texture_;
    std::optional<MeshBuffer>       meshBuffer_;
    std::optional<MeshPipeline>     meshPipeline_;
    std::optional<ParticlePipeline> particlePipeline_;
    std::vector<GameObject>         gameObjects_;

    void initWindow();
    void initVulkan();
    void mainLoop();
    void cleanup();
    void drawFrame();
    void updateUniforms();
    void recreateSwapchain();
    void recordCommandBuffer(uint32_t imageIndex);
    void recordComputeCommandBuffer(uint32_t frameIdx);
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};
