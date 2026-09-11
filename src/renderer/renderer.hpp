#pragma once

#include <map>
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
#include "renderer/light_buffer.hpp"
#include "renderer/mesh_buffer.hpp"
#include "renderer/orbit_controls.hpp"
#include "renderer/particle_pipeline.hpp"
#include "renderer/render_graph.hpp"
#include "renderer/render_object.hpp"
#include "renderer/resource_manager.hpp"
#include "scene/scene_loader.hpp"
#include "scene/types.hpp"

class Renderer {
public:
    explicit Renderer(std::string scenePath, std::string screenshotPath = {});
    void run();

private:
    std::string mScenePath;
    std::string mScreenshotPath;
    Scene mScene;
    Camera mCamera;
    OrbitControls mOrbitControls;
    GLFWwindow* mWindow = nullptr;
    uint32_t mFrameIndex = 0;
    bool mFramebufferResized = false;

    RenderGraph mRenderGraph;
    RenderGraphImageHandle mSwapchainImageHandle{}; // updated per-frame via updateImportedImage

    std::optional<VulkanContext> mCtx;
    std::optional<Swapchain> mSwapchain;
    std::optional<CommandService> mCmds;
    std::optional<SyncObjects> mSync;
    std::optional<ResourceManager> mResources;
    std::optional<MeshBuffer> mMeshBuffer;
    std::map<std::string, Material> mMaterials;
    std::optional<LightBuffer> mLightBuffer;
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
    void buildRenderGraph();
    void recordCommandBuffer(uint32_t imageIndex);
    void recordComputeCommandBuffer(uint32_t frameIdx);
    void captureScreenshot(uint32_t imageIndex);
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
};
