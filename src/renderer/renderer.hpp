#pragma once

#include <optional>
#include <string>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "core/command_service.hpp"
#include "core/context.hpp"
#include "core/swapchain.hpp"
#include "core/sync.hpp"
#include "renderer/camera.hpp"
#include "renderer/mesh_buffer.hpp"
#include "renderer/particle_pipeline.hpp"
#include "renderer/render_object.hpp"
#include "renderer/resource_manager.hpp"
#include "scene/scene_loader.hpp"
#include "scene/types.hpp"

class Renderer {
public:
    explicit Renderer(std::string scenePath);
    void run();

private:
    std::string mScenePath;
    Scene mScene;
    Camera mCamera;
    GLFWwindow* mWindow = nullptr;
    uint32_t mFrameIndex = 0;
    bool mFramebufferResized = false;

    std::optional<VulkanContext> mCtx;
    std::optional<Swapchain> mSwapchain;
    std::optional<CommandService> mCmds;
    std::optional<SyncObjects> mSync;
    std::optional<ResourceManager> mResources;
    std::optional<MeshBuffer> mMeshBuffer;
    std::optional<Material> mMaterial;
    std::optional<ParticlePipeline> mParticlePipeline;
    std::vector<GameObject> mGameObjects;
    std::vector<RenderObject> mRenderObjects;

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
