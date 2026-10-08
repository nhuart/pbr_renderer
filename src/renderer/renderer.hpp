#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "core/camera.hpp"
#include "core/command_service.hpp"
#include "core/config.hpp"
#include "core/context.hpp"
#include "core/swapchain.hpp"
#include "core/sync.hpp"
#include "renderer/ao_pipeline.hpp"
#include "renderer/game_object.hpp"
#include "renderer/gpu_types.hpp"
#include "renderer/ibl_environment.hpp"
#include "renderer/light_buffer.hpp"
#include "renderer/mesh_buffer.hpp"
#include "renderer/orbit_controls.hpp"
#include "renderer/particle_pipeline.hpp"
#include "renderer/render_graph.hpp"
#include "renderer/render_object.hpp"
#include "renderer/resource_manager.hpp"
#include "renderer/shadow_pipeline.hpp"
#include "renderer/skybox_pipeline.hpp"
#include "renderer/tonemap_pipeline.hpp"
#include "scene/scene_loader.hpp"
#include "scene/types.hpp"

class Renderer {
public:
    explicit Renderer(std::string scenePath, std::string screenshotPath = {},
            bool exitAfterScreenshot = false);
    void run();

private:
    std::string mScenePath;
    std::string mScreenshotPath;
    bool mExitAfterScreenshot = false;
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
    std::optional<TonemapPipeline> mTonemapPipeline;
    std::optional<CommandService> mCmds;
    std::optional<SyncObjects> mSync;
    std::optional<ResourceManager> mResources;
    std::optional<MeshBuffer> mMeshBuffer;
    std::map<std::string, Material> mMaterials;
    std::optional<LightBuffer> mLightBuffer;
    std::optional<IblEnvironment> mIblEnvironment;
    std::optional<ShadowPipeline> mShadowPipeline;
    RenderGraphImageHandle mShadowMapImageHandle{};
    std::optional<SkyboxPipeline> mSkyboxPipeline;
    std::optional<ParticlePipeline> mParticlePipeline;
    std::optional<AoPipeline> mAoPipeline;
    RenderGraphImageHandle mNormalsImageHandle{};
    RenderGraphImageHandle mAoRawImageHandle{};
    RenderGraphImageHandle mAoBlurImageHandle{};
    RenderGraphImageHandle mDepthPrepassImageHandle{};
    std::vector<RenderGraphImageHandle> mDepthMipImageHandles;
    std::vector<GameObject> mGameObjects;
    std::vector<RenderObject> mRenderObjects;

    void initWindow();
    void initVulkan();
    void createSceneObjects();
    void createEnvironment();
    void createShadowPipeline();
    void createAoPipeline();
    void createTonemapPipeline();
    void createMaterials();
    void createRenderObjects();
    void resolveShaderVariants();
    void mainLoop();
    void cleanup();
    void drawFrame();
    void updateUniforms();
    void updateLights(float exposure);
    void updateMaterialUniforms(glm::mat4 const& view, glm::mat4 const& projection,
            glm::vec3 const& cameraPosition, float exposure);
    void updateShadowUniforms();
    void updateAoUniforms(glm::mat4 const& view, glm::mat4 const& projection);
    void recreateSwapchain();
    void buildRenderGraph();
    [[nodiscard]] RenderGraphImageHandle importHdrColorImage();
    void addTonemapPass(RenderGraphImageHandle hdrColor);
    void recordCommandBuffer(uint32_t imageIndex);
    void recordComputeCommandBuffer(uint32_t frameIdx);
    void captureScreenshot(uint32_t imageIndex);
    void captureShadowMapDebug();
    void captureAoTextureDebug();
    void captureNormalsTextureDebug();
    [[nodiscard]] std::string screenshotOutputPath(std::string const& filename) const;
    using PixelTransform = std::function<std::vector<uint8_t>(void const*, uint32_t pixelCount)>;
    void captureImageToPng(vk::Image image, vk::ImageLayout currentLayout,
            vk::ImageAspectFlags aspect, uint32_t width, uint32_t height, uint32_t bytesPerPixel,
            std::string const& outputPath, PixelTransform transform = {});
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
};
