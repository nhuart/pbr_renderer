#include "renderer/renderer.hpp"
#include "core/resource_allocator.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

#include <stb_image_write.h>


#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

static AmbientLight const* ambientLightFromLights(std::vector<Light> const& lights) {
    for (auto const& light: lights) {
        if (auto const* a = std::get_if<AmbientLight>(&light)) {
            return a;
        }
    }
    return nullptr;
}

Renderer::Renderer(std::string scenePath, std::string screenshotPath)
        : mScenePath(std::move(scenePath)),
          mScreenshotPath(std::move(screenshotPath)) {}

void Renderer::run() {
    mScene = loadScene(mScenePath);
    mCamera = mScene.camera;
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
}

void Renderer::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    mWindow = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(mWindow, this);
    glfwSetFramebufferSizeCallback(mWindow, framebufferResizeCallback);
    glfwSetMouseButtonCallback(mWindow, mouseButtonCallback);
    glfwSetCursorPosCallback(mWindow, cursorPosCallback);
    glfwSetScrollCallback(mWindow, scrollCallback);
}

void Renderer::initVulkan() {
    mCtx.emplace(mWindow);
    mSwapchain.emplace(*mCtx, mWindow);
    mCmds.emplace(*mCtx);
    mCmds->allocateCommandBuffers(*mCtx);

    for (uint32_t i = 0; i < mScene.meshInstances.size(); ++i) {
        auto const& inst = mScene.meshInstances[i];
        GameObject obj;
        obj.position = inst.position;
        obj.rotation = glm::radians(inst.rotation);
        obj.scale = inst.scale;
        mGameObjects.push_back(std::move(obj));

        RenderObject ro;
        ro.gameObjectIndex = i;
        mRenderObjects.push_back(std::move(ro));
    }
    std::cout << "Game objects: " << mGameObjects.size() << " created\n";

    mResources.emplace();
    mMeshBuffer.emplace(*mCtx, *mCmds, mScene);
    mLightBuffer.emplace(*mCtx, LightUBO{});

    if (mScene.iblPath) {
        auto iblName = std::filesystem::path(*mScene.iblPath).filename().string();
        mIblEnvironment.emplace(*mCtx, *mCmds, *mScene.iblPath + "/" + iblName + "_ibl.ktx",
                *mScene.iblPath + "/sh.txt");
        if (mScene.skybox) {
            mSkyboxPipeline.emplace(*mCtx, *mSwapchain, *mIblEnvironment);
        }
    }

    // Determine if any directional light casts a shadow
    DirectionalLight const* shadowCastingLight = nullptr;
    for (auto const& light: mScene.lights) {
        if (auto const* dl = std::get_if<DirectionalLight>(&light)) {
            if (dl->castShadow && !shadowCastingLight) {
                shadowCastingLight = dl;
            }
        }
    }
    if (shadowCastingLight) {
        uint32_t shadowCasterCount = static_cast<uint32_t>(
                std::count_if(mScene.meshInstances.begin(), mScene.meshInstances.end(),
                        [](MeshInstance const& inst) { return inst.castShadows; }));
        mShadowMap.emplace(*mCtx, *mCmds, shadowCasterCount);
        mShadowMap->shadowType = shadowCastingLight->shadowType;
        mShadowMap->shadowBias = shadowCastingLight->shadowBias;
        mShadowMap->updateLightSpaceMatrix(*shadowCastingLight);
    }

    resolveShaderVariants();

    for (auto const& inst: mScene.meshInstances) {
        bool doubleSided = mMeshBuffer->meshRanges.at(inst.gltfPath).doubleSided;
        auto key = inst.vertexShader + "+" + inst.resolvedFragShader + (doubleSided ? "+ds" : "");
        if (!mMaterials.contains(key)) {
            mMaterials.emplace(key,
                    Material(*mCtx, *mSwapchain, inst.vertexShader, inst.resolvedFragShader,
                            inst.shaderFeatures, doubleSided));
        }
    }

    for (auto& ro: mRenderObjects) {
        auto const& inst = mScene.meshInstances[ro.gameObjectIndex];
        ro.range = mMeshBuffer->meshRanges.at(inst.gltfPath);
        ro.texture = &mResources->getTexture(*mCtx, *mCmds, inst.texturePath, inst.gltfPath,
                &*mMeshBuffer);
        ro.normalMap = &mResources->getNormalMap(*mCtx, *mCmds, inst.gltfPath, *mMeshBuffer);
        bool doubleSided = ro.range.doubleSided;
        auto key = inst.vertexShader + "+" + inst.resolvedFragShader + (doubleSided ? "+ds" : "");
        ro.material = &mMaterials.at(key);
        ro.materialInstance = ro.material->createInstance(*mCtx, *ro.texture, mLightBuffer->buffer,
                hasFeature(inst.shaderFeatures, ShaderFeatures::Ibl) ? &*mIblEnvironment : nullptr,
                hasFeature(inst.shaderFeatures, ShaderFeatures::NormalMap) ? ro.normalMap : nullptr,
                (hasFeature(inst.shaderFeatures, ShaderFeatures::HardShadow) ||
                        hasFeature(inst.shaderFeatures, ShaderFeatures::PcfShadow))
                        ? &*mShadowMap
                        : nullptr);
    }

    if (mScene.particles) {
        mParticlePipeline.emplace(*mCtx, *mSwapchain, *mCmds, *mScene.particles);
        mCmds->allocateComputeCommandBuffers(*mCtx);
    }

    mSync.emplace(*mCtx, static_cast<uint32_t>(mSwapchain->images.size()),
            mScene.particles.has_value());

    buildRenderGraph();
}

void Renderer::mainLoop() {
    while (!glfwWindowShouldClose(mWindow)) {
        glfwPollEvents();
        drawFrame();
    }
    mCtx->device.waitIdle();
}

void Renderer::cleanup() {
    glfwDestroyWindow(mWindow);
    glfwTerminate();
}

void Renderer::framebufferResizeCallback(GLFWwindow* window, int /*width*/, int /*height*/) {
    auto* app = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    app->mFramebufferResized = true;
}

void Renderer::mouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/) {
    auto* app = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    double x{}, y{};
    glfwGetCursorPos(window, &x, &y);
    app->mOrbitControls.mouseButton(button, action, x, y);
}

void Renderer::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    auto* app = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    app->mOrbitControls.mouseMove(app->mCamera, xpos, ypos);
}

void Renderer::scrollCallback(GLFWwindow* window, double /*xoffset*/, double yoffset) {
    auto* app = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    app->mOrbitControls.scroll(app->mCamera, yoffset);
}

void Renderer::recreateSwapchain() {
    mCtx->device.waitIdle();
    mSwapchain->recreate(*mCtx, mWindow);
    mSync->recreatePresent(*mCtx, static_cast<uint32_t>(mSwapchain->images.size()));
    buildRenderGraph();
}

void Renderer::updateUniforms() {
    static auto lastTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count() * 1000.0f;
    lastTime = currentTime;

    float aspect = static_cast<float>(mSwapchain->extent.width) /
                   static_cast<float>(mSwapchain->extent.height);
    glm::mat4 view = mCamera.viewMatrix();
    glm::mat4 proj = mCamera.projMatrix(aspect);

    glm::vec3 camPos = mCamera.position();

    // Lights are static but the UBO is re-uploaded every frame for simplicity.
    // exposure = 1 / (1.2 × aperture² / shutter × 100 / ISO)  (matches Filament's Exposure.cpp)
    float exposure =
            1.0f / (1.2f * static_cast<float>(mScene.camera.aperture * mScene.camera.aperture /
                                              mScene.camera.shutterSpeed * 100.0 /
                                              mScene.camera.sensitivity));

    LightUBO lightUbo{};
    uint32_t lightCount = 0;
    for (auto const& light: mScene.lights) {
        if (lightCount >= MAX_LIGHTS) {
            std::cerr << "Warning: scene has more than " << MAX_LIGHTS
                      << " lights; excess lights will be ignored.\n";
            break;
        }
        if (std::holds_alternative<AmbientLight>(light)) {
            continue;
        }
        lightUbo.lights[lightCount++] = std::visit(
                [exposure](auto const& l) { return GpuLight::from(l, exposure); }, light);
    }
    lightUbo.counts = glm::uvec4(lightCount, 0, 0, 0);
    memcpy(mLightBuffer->mapped, &lightUbo, sizeof(lightUbo));

    for (auto& ro: mRenderObjects) {
        auto const& obj = mGameObjects[ro.gameObjectIndex];
        glm::mat4 model = obj.getModelMatrix();
        auto const& meshInst = mScene.meshInstances[ro.gameObjectIndex];

        UniformBufferObject ubo{
            .model = model,
            .view = view,
            .proj = proj,
            .normalMatrix = glm::transpose(glm::inverse(model)),
            .baseColor = meshInst.baseColor,
            .cameraPos = glm::vec4(camPos, 0.0f),
            .pbrParams = glm::vec4(
                    meshInst.metallic, meshInst.roughness,
                    [&] {
                        // Matches Filament: sh0 = intensity/sqrt(4π), Fd = sh0 * iblLuminance
                        // (diffuseBRDF=1 since irradiance() coefficients are not pre-divided by π)
                        constexpr float InvSqrt4Pi = 1.0f / 3.54490770181f; // 1/sqrt(4π)
                        auto const* ambientLight = ambientLightFromLights(mScene.lights);
                        return ambientLight ? ambientLight->intensity * InvSqrt4Pi *
                                                      ambientLight->iblIntensity * exposure
                                            : 0.0f;
                    }(),
                    0.0f),
        };
        ro.materialInstance.updateUBO(mFrameIndex, ubo);
    }

    if (mSkyboxPipeline) {
        SkyboxUBO skyboxUbo{
            .invProj = glm::inverse(proj),
            .invView = glm::inverse(view),
        };
        mSkyboxPipeline->updateUBO(mFrameIndex, skyboxUbo);
    }

    if (mShadowMap) {
        uint32_t shadowObjIdx = 0;
        for (size_t i = 0; i < mScene.meshInstances.size(); ++i) {
            if (mScene.meshInstances[i].castShadows) {
                mShadowMap->updateObjectUBO(shadowObjIdx++, mFrameIndex,
                        mGameObjects[i].getModelMatrix());
            }
        }
        mShadowMap->updateFragmentUBO(mFrameIndex);
    }

    if (mScene.particles) {
        ComputeUBO cubo{ .deltaTime = deltaTime };
        memcpy(mParticlePipeline->computeUniformBuffersMapped[mFrameIndex], &cubo, sizeof(cubo));
    }
}

void Renderer::recordComputeCommandBuffer(uint32_t frameIdx) {
    auto const& cmd = mCmds->computeCommandBuffers[frameIdx];
    cmd.begin({});
    if (mScene.particles) {
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *mParticlePipeline->computePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                *mParticlePipeline->computePipelineLayout, 0,
                *mParticlePipeline->computeDescriptorSets[frameIdx], {});
        cmd.dispatch(mScene.particles->count / 256, 1, 1);
    }
    cmd.end();
}

void Renderer::captureScreenshot(uint32_t imageIndex) {
    uint32_t width = mSwapchain->extent.width;
    uint32_t height = mSwapchain->extent.height;
    vk::DeviceSize bufferSize = vk::DeviceSize(width) * height * 4;

    auto [readbackBuffer, readbackMemory] = vkutil::createBuffer(*mCtx, bufferSize,
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);

    vk::raii::CommandBuffer cmd =
            std::move(mCtx->device
                              .allocateCommandBuffers(vk::CommandBufferAllocateInfo{
                                  .commandPool = *mCmds->commandPool,
                                  .level = vk::CommandBufferLevel::ePrimary,
                                  .commandBufferCount = 1,
                              })
                              .front());

    cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

    vk::Image srcImage = mSwapchain->images[imageIndex];

    vkutil::transitionImageLayout(cmd, srcImage, vk::ImageLayout::ePresentSrcKHR,
            vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eNone,
            vk::AccessFlagBits2::eTransferRead, vk::PipelineStageFlagBits2::eBottomOfPipe,
            vk::PipelineStageFlagBits2::eTransfer);

    vk::BufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { width, height, 1 },
    };
    cmd.copyImageToBuffer(srcImage, vk::ImageLayout::eTransferSrcOptimal, *readbackBuffer, region);

    vkutil::transitionImageLayout(cmd, srcImage, vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageLayout::ePresentSrcKHR, vk::AccessFlagBits2::eTransferRead,
            vk::AccessFlagBits2::eNone, vk::PipelineStageFlagBits2::eTransfer,
            vk::PipelineStageFlagBits2::eBottomOfPipe);

    cmd.end();

    vk::raii::Fence fence(mCtx->device, vk::FenceCreateInfo{});
    vk::CommandBuffer cmdHandle = *cmd;
    mCtx->graphicsQueue.submit(
            vk::SubmitInfo{
                .commandBufferCount = 1,
                .pCommandBuffers = &cmdHandle,
            },
            *fence);

    std::ignore =
            mCtx->device.waitForFences(*fence, vk::True, std::numeric_limits<uint64_t>::max());

    auto* pixels = static_cast<uint8_t*>(readbackMemory.mapMemory(0, bufferSize));
    mCtx->device.invalidateMappedMemoryRanges(
            vk::MappedMemoryRange{ .memory = *readbackMemory, .offset = 0, .size = bufferSize });
    for (uint32_t i = 0; i < width * height; ++i) {
        std::swap(pixels[i * 4 + 0], pixels[i * 4 + 2]); // BGRA -> RGBA
        pixels[i * 4 + 3] = 255;
    }
    stbi_write_png(mScreenshotPath.c_str(), static_cast<int>(width), static_cast<int>(height), 4,
            pixels, static_cast<int>(width) * 4);
    readbackMemory.unmapMemory();
    std::cout << "Screenshot saved: " << mScreenshotPath << "\n";

    if (mShadowMap) {
        captureShadowMapDebug();
    }
}

// Converts raw depth floats (sampler border = 1.0) to normalized grayscale RGBA bytes.
// Geometry depth is remapped to [0,1] within its own range and inverted (closer = brighter).
// Background pixels (depth == 1.0) map to black.
static std::vector<uint8_t> depthFloatsToGrayscaleRgba(float const* depthPixels,
        uint32_t pixelCount) {
    constexpr float kBorderDepth = 1.0f;

    float minGeometryDepth = kBorderDepth;
    float maxGeometryDepth = 0.0f;
    for (uint32_t i = 0; i < pixelCount; ++i) {
        float depth = depthPixels[i];
        if (depth < kBorderDepth) {
            minGeometryDepth = std::min(minGeometryDepth, depth);
            maxGeometryDepth = std::max(maxGeometryDepth, depth);
        }
    }
    float geometryDepthRange = maxGeometryDepth - minGeometryDepth;
    if (geometryDepthRange < 1e-5f) {
        geometryDepthRange = 1.0f;
    }

    std::vector<uint8_t> rgba(pixelCount * 4);
    for (uint32_t i = 0; i < pixelCount; ++i) {
        float depth = depthPixels[i];
        uint8_t grayscale;
        if (depth >= kBorderDepth) {
            grayscale = 0; // background → black
        } else {
            float normalizedDepth = (depth - minGeometryDepth) / geometryDepthRange;
            grayscale = static_cast<uint8_t>((1.0f - normalizedDepth) * 255.0f);
        }
        rgba[i * 4 + 0] = grayscale;
        rgba[i * 4 + 1] = grayscale;
        rgba[i * 4 + 2] = grayscale;
        rgba[i * 4 + 3] = 255;
    }
    return rgba;
}

void Renderer::captureShadowMapDebug() {
    constexpr uint32_t shadowMapSize = SHADOW_MAP_SIZE;
    constexpr uint32_t pixelCount = shadowMapSize * shadowMapSize;
    // Depth image is R32 float (or D32/D24) — copy as 4 bytes per pixel
    vk::DeviceSize readbackBufferSize = vk::DeviceSize(pixelCount) * sizeof(float);

    auto [readbackBuffer, readbackMemory] = vkutil::createBuffer(*mCtx, readbackBufferSize,
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);

    vk::raii::CommandBuffer cmd =
            std::move(mCtx->device
                              .allocateCommandBuffers(vk::CommandBufferAllocateInfo{
                                  .commandPool = *mCmds->commandPool,
                                  .level = vk::CommandBufferLevel::ePrimary,
                                  .commandBufferCount = 1,
                              })
                              .front());

    cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

    vkutil::transitionImageLayout(cmd, *mShadowMap->image, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eShaderRead,
            vk::AccessFlagBits2::eTransferRead, vk::PipelineStageFlagBits2::eFragmentShader,
            vk::PipelineStageFlagBits2::eTransfer, vk::ImageAspectFlagBits::eDepth);

    vk::BufferImageCopy copyRegion{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = vk::ImageAspectFlagBits::eDepth,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { shadowMapSize, shadowMapSize, 1 },
    };
    cmd.copyImageToBuffer(*mShadowMap->image, vk::ImageLayout::eTransferSrcOptimal, *readbackBuffer,
            copyRegion);

    vkutil::transitionImageLayout(cmd, *mShadowMap->image, vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits2::eTransferRead,
            vk::AccessFlagBits2::eShaderRead, vk::PipelineStageFlagBits2::eTransfer,
            vk::PipelineStageFlagBits2::eFragmentShader, vk::ImageAspectFlagBits::eDepth);

    cmd.end();

    vk::raii::Fence fence(mCtx->device, vk::FenceCreateInfo{});
    vk::CommandBuffer cmdHandle = *cmd;
    mCtx->graphicsQueue.submit(
            vk::SubmitInfo{ .commandBufferCount = 1, .pCommandBuffers = &cmdHandle }, *fence);
    if (mCtx->device.waitForFences(*fence, vk::True, std::numeric_limits<uint64_t>::max()) !=
            vk::Result::eSuccess) {
        throw std::runtime_error("shadow map debug capture: fence wait failed");
    }

    auto* depthPixels = static_cast<float*>(readbackMemory.mapMemory(0, readbackBufferSize));
    mCtx->device.invalidateMappedMemoryRanges(vk::MappedMemoryRange{ .memory = *readbackMemory,
        .offset = 0,
        .size = readbackBufferSize });

    std::vector<uint8_t> rgba = depthFloatsToGrayscaleRgba(depthPixels, pixelCount);
    readbackMemory.unmapMemory();

    // Always write to shadow_depth.png in the same directory as the screenshot
    std::string debugPath = mScreenshotPath;
    auto slash = debugPath.rfind('/');
    debugPath =
            (slash != std::string::npos ? debugPath.substr(0, slash + 1) : "") + "shadow_depth.png";

    stbi_write_png(debugPath.c_str(), static_cast<int>(shadowMapSize),
            static_cast<int>(shadowMapSize), 4, rgba.data(), static_cast<int>(shadowMapSize) * 4);
    std::cout << "Shadow depth map saved: " << debugPath << "\n";
}

void Renderer::resolveShaderVariants() {
    for (auto& inst: mScene.meshInstances) {
        if (inst.fragmentShader != "pbr") {
            inst.resolvedFragShader = inst.fragmentShader;
            continue;
        }

        bool hasNormalMap = inst.useNormalMap && mMeshBuffer->normalMaps.contains(inst.gltfPath);
        bool hasShadow = inst.receiveShadows && mShadowMap.has_value();
        bool hasPcf = hasShadow && mShadowMap->shadowType == ShadowType::PCF;

        ShaderFeatures features = ShaderFeatures::None;
        std::string frag = "pbr";

        if (mIblEnvironment) {
            features = features | ShaderFeatures::Ibl;
            frag += "_ibl";
        }
        if (hasNormalMap) {
            features = features | ShaderFeatures::NormalMap;
            frag += "_normal";
        }
        if (hasPcf) {
            features = features | ShaderFeatures::PcfShadow;
            frag += "_pcf";
        } else if (hasShadow) {
            features = features | ShaderFeatures::HardShadow;
            frag += "_shadow";
        }

        inst.resolvedFragShader = frag;
        inst.shaderFeatures = features;
    }
}

void Renderer::buildRenderGraph() {
    mRenderGraph = RenderGraph{};
    mShadowMapImageHandle = {};

    vk::Extent2D swapchainExtent = mSwapchain->extent;

    // Import swapchain-provided images — the graph records transitions but does not own them.
    // updateImportedImage() updates the swapchain backing each frame.
    mSwapchainImageHandle =
            mRenderGraph.importImage("swapchain", mSwapchain->images[0], *mSwapchain->imageViews[0],
                    RenderGraphImage{
                        .format = mSwapchain->surfaceFormat.format,
                        .extent = swapchainExtent,
                        .usage = vk::ImageUsageFlagBits::eColorAttachment,
                        .aspect = vk::ImageAspectFlagBits::eColor,
                        .samples = vk::SampleCountFlagBits::e1,
                    });

    auto colorImage =
            mRenderGraph.importImage("color", *mSwapchain->colorImage, *mSwapchain->colorImageView,
                    RenderGraphImage{
                        .format = mSwapchain->surfaceFormat.format,
                        .extent = swapchainExtent,
                        .usage = vk::ImageUsageFlagBits::eColorAttachment,
                        .aspect = vk::ImageAspectFlagBits::eColor,
                        .samples = mCtx->msaaSamples,
                    });

    auto depthImage =
            mRenderGraph.importImage("depth", *mSwapchain->depthImage, *mSwapchain->depthImageView,
                    RenderGraphImage{
                        .format = vkutil::findDepthFormat(*mCtx),
                        .extent = swapchainExtent,
                        .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
                        .aspect = vk::ImageAspectFlagBits::eDepth,
                        .samples = mCtx->msaaSamples,
                    });

    if (mShadowMap) {
        vk::Extent2D shadowExtent{ SHADOW_MAP_SIZE, SHADOW_MAP_SIZE };
        mShadowMapImageHandle =
                mRenderGraph.importImage("shadowMap", *mShadowMap->image, *mShadowMap->imageView,
                        RenderGraphImage{
                            .format = vkutil::findDepthFormat(*mCtx),
                            .extent = shadowExtent,
                            .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment |
                                     vk::ImageUsageFlagBits::eSampled |
                                     vk::ImageUsageFlagBits::eTransferSrc,
                            .aspect = vk::ImageAspectFlagBits::eDepth,
                            .samples = vk::SampleCountFlagBits::e1,
                        },
                        vk::ImageLayout::eShaderReadOnlyOptimal);

        mRenderGraph.addPass("ShadowPass")
                .writesDepth(mShadowMapImageHandle)
                .execute([this](vk::raii::CommandBuffer const& cmd) {
                    cmd.setViewport(0, vk::Viewport{
                                           .x = 0.0f,
                                           .y = 0.0f,
                                           .width = static_cast<float>(SHADOW_MAP_SIZE),
                                           .height = static_cast<float>(SHADOW_MAP_SIZE),
                                           .minDepth = 0.0f,
                                           .maxDepth = 1.0f,
                                       });
                    cmd.setScissor(0, vk::Rect2D{ .offset = { 0, 0 },
                                          .extent = { SHADOW_MAP_SIZE, SHADOW_MAP_SIZE } });
                    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *mShadowMap->pipeline);
                    cmd.bindVertexBuffers(0, *mMeshBuffer->vertexBuffer, { vk::DeviceSize{ 0 } });
                    cmd.bindIndexBuffer(*mMeshBuffer->indexBuffer, 0, vk::IndexType::eUint32);
                    uint32_t shadowObjIdx = 0;
                    for (size_t i = 0; i < mRenderObjects.size(); ++i) {
                        auto const& inst = mScene.meshInstances[i];
                        if (!inst.castShadows) {
                            continue;
                        }
                        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                *mShadowMap->pipelineLayout, 0,
                                *mShadowMap->objects[shadowObjIdx].descriptorSets[mFrameIndex], {});
                        cmd.drawIndexed(mRenderObjects[i].range.indexCount, 1,
                                mRenderObjects[i].range.firstIndex, 0, 0);
                        ++shadowObjIdx;
                    }
                });
    }

    auto& forwardPass = mRenderGraph.addPass("ForwardPass")
                                .writesColor(colorImage)
                                .resolvesTo(mSwapchainImageHandle)
                                .writesDepth(depthImage);
    if (mShadowMapImageHandle.isValid()) {
        forwardPass.reads(mShadowMapImageHandle);
    }
    forwardPass.execute([this](vk::raii::CommandBuffer const& commandBuffer) {
        vk::Extent2D drawExtent = mSwapchain->extent;
        commandBuffer.bindVertexBuffers(0, *mMeshBuffer->vertexBuffer, { vk::DeviceSize{ 0 } });
        commandBuffer.bindIndexBuffer(*mMeshBuffer->indexBuffer, 0, vk::IndexType::eUint32);
        commandBuffer.setViewport(0, vk::Viewport{
                                         .x = 0.0f,
                                         .y = 0.0f,
                                         .width = static_cast<float>(drawExtent.width),
                                         .height = static_cast<float>(drawExtent.height),
                                         .minDepth = 0.0f,
                                         .maxDepth = 1.0f,
                                     });
        commandBuffer.setScissor(0, vk::Rect2D{ .offset = { 0, 0 }, .extent = drawExtent });

        for (auto const& renderObject: mRenderObjects) {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                    *renderObject.material->pipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                    *renderObject.material->pipelineLayout, 0,
                    *renderObject.materialInstance.descriptorSets[mFrameIndex], {});
            commandBuffer.drawIndexed(renderObject.range.indexCount, 1,
                    renderObject.range.firstIndex, 0, 0);
        }

        if (mScene.particles) {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                    *mParticlePipeline->particlePipeline);
            commandBuffer.bindVertexBuffers(0,
                    *mParticlePipeline->shaderStorageBuffers[mFrameIndex], { vk::DeviceSize{ 0 } });
            commandBuffer.draw(mScene.particles->count, 1, 0, 0);
        }

        if (mSkyboxPipeline) {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                    *mSkyboxPipeline->pipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                    *mSkyboxPipeline->pipelineLayout, 0,
                    *mSkyboxPipeline->descriptorSets[mFrameIndex], {});
            commandBuffer.draw(3, 1, 0, 0);
        }
    });

    mRenderGraph.compile(*mCtx);
}

void Renderer::recordCommandBuffer(uint32_t imageIndex) {
    // Point the imported swapchain slot at the current frame's image — no graph rebuild.
    mRenderGraph.updateImportedImage(mSwapchainImageHandle, mSwapchain->images[imageIndex],
            *mSwapchain->imageViews[imageIndex]);

    auto const& commandBuffer = mCmds->commandBuffers[mFrameIndex];
    commandBuffer.begin({});
    mRenderGraph.execute(commandBuffer);
    commandBuffer.end();
}

void Renderer::drawFrame() {
    if (mScene.particles) {
        std::ignore = mCtx->device.waitForFences(*mSync->computeInFlightFences[mFrameIndex],
                vk::True, std::numeric_limits<uint64_t>::max());
        mCtx->device.resetFences(*mSync->computeInFlightFences[mFrameIndex]);
    }

    updateUniforms();

    if (mScene.particles) {
        mCmds->computeCommandBuffers[mFrameIndex].reset();
        recordComputeCommandBuffer(mFrameIndex);

        vk::CommandBuffer computeCmdBuf = *mCmds->computeCommandBuffers[mFrameIndex];
        vk::SubmitInfo computeSubmitInfo{
            .commandBufferCount = 1,
            .pCommandBuffers = &computeCmdBuf,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &*mSync->computeFinishedSemaphores[mFrameIndex],
        };
        mCtx->computeQueue.submit(computeSubmitInfo, *mSync->computeInFlightFences[mFrameIndex]);
    }

    std::ignore = mCtx->device.waitForFences(*mSync->inFlightFences[mFrameIndex], vk::True,
            std::numeric_limits<uint64_t>::max());

    auto [acquireResult, imageIndex] =
            mSwapchain->swapChain.acquireNextImage(std::numeric_limits<uint64_t>::max(),
                    *mSync->presentCompleteSemaphores[mFrameIndex], nullptr);

    if (acquireResult == vk::Result::eErrorOutOfDateKHR) {
        recreateSwapchain();
        return;
    }
    if (acquireResult != vk::Result::eSuccess && acquireResult != vk::Result::eSuboptimalKHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    mCtx->device.resetFences(*mSync->inFlightFences[mFrameIndex]);

    mCmds->commandBuffers[mFrameIndex].reset();
    recordCommandBuffer(imageIndex);

    std::vector<vk::Semaphore> waitSemaphores = { *mSync->presentCompleteSemaphores[mFrameIndex] };
    std::vector<vk::PipelineStageFlags> waitStages = {
        vk::PipelineStageFlagBits::eColorAttachmentOutput
    };
    if (mScene.particles) {
        waitSemaphores.push_back(*mSync->computeFinishedSemaphores[mFrameIndex]);
        waitStages.push_back(vk::PipelineStageFlagBits::eVertexInput);
    }
    vk::CommandBuffer cmdBuf = *mCmds->commandBuffers[mFrameIndex];
    vk::SubmitInfo submitInfo{
        .waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size()),
        .pWaitSemaphores = waitSemaphores.data(),
        .pWaitDstStageMask = waitStages.data(),
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdBuf,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &*mSync->renderFinishedSemaphores[imageIndex],
    };
    mCtx->graphicsQueue.submit(submitInfo, *mSync->inFlightFences[mFrameIndex]);

    vk::SwapchainKHR swapChainHandle = *mSwapchain->swapChain;
    vk::PresentInfoKHR presentInfo{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*mSync->renderFinishedSemaphores[imageIndex],
        .swapchainCount = 1,
        .pSwapchains = &swapChainHandle,
        .pImageIndices = &imageIndex,
    };
    vk::Result presentResult = mCtx->graphicsQueue.presentKHR(presentInfo);

    if (!mScreenshotPath.empty() && (presentResult == vk::Result::eSuccess ||
                                            presentResult == vk::Result::eSuboptimalKHR)) {
        captureScreenshot(imageIndex);
        mScreenshotPath.clear();
    }

    if (presentResult == vk::Result::eErrorOutOfDateKHR) {
        if (!mScreenshotPath.empty()) {
            // Swapchain out of date but we need a screenshot — grab it from the acquired image
            // before the swapchain is destroyed.
            captureScreenshot(imageIndex);
            mScreenshotPath.clear();
        }
        mFramebufferResized = false;
        recreateSwapchain();
    } else if (presentResult == vk::Result::eSuboptimalKHR || mFramebufferResized) {
        mFramebufferResized = false;
        recreateSwapchain();
    } else if (presentResult != vk::Result::eSuccess) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    mFrameIndex = (mFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}
