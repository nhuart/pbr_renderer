#include "renderer/renderer.hpp"
#include "core/resource_allocator.hpp"
#include "renderer/ao_pipeline.hpp"

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

static std::string materialKey(MeshInstance const& instance, bool doubleSided) {
    return instance.vertexShader + "+" + instance.resolvedFragShader + (doubleSided ? "+ds" : "");
}

static float diffuseIblLuminance(std::vector<Light> const& lights, float exposure) {
    auto const* ambientLight = ambientLightFromLights(lights);
    if (!ambientLight) {
        return 0.0f;
    }
    // Matches Filament: sh0 = intensity/sqrt(4π), Fd = sh0 * iblLuminance.
    // diffuseBRDF=1 because irradiance() coefficients are not pre-divided by π.
    constexpr float InvSqrt4Pi = 1.0f / 3.54490770181f;
    return ambientLight->intensity * InvSqrt4Pi * ambientLight->iblIntensity * exposure;
}

static void setViewportAndScissor(vk::raii::CommandBuffer const& cmd, vk::Extent2D extent) {
    cmd.setViewport(0, vk::Viewport{
                           .x = 0.0f,
                           .y = 0.0f,
                           .width = static_cast<float>(extent.width),
                           .height = static_cast<float>(extent.height),
                           .minDepth = 0.0f,
                           .maxDepth = 1.0f,
                       });
    cmd.setScissor(0, vk::Rect2D{ .offset = { 0, 0 }, .extent = extent });
}

Renderer::Renderer(std::string scenePath, std::string screenshotPath, bool exitAfterScreenshot)
        : mScenePath(std::move(scenePath)),
          mScreenshotPath(std::move(screenshotPath)),
          mExitAfterScreenshot(exitAfterScreenshot) {}

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

    createSceneObjects();
    mResources.emplace();
    mMeshBuffer.emplace(*mCtx, *mCmds, mScene);
    mLightBuffer.emplace(*mCtx, LightUBO{});
    createEnvironment();
    createShadowPipeline();
    createAoPipeline();
    resolveShaderVariants();
    createMaterials();
    createRenderObjects();

    if (mScene.particles) {
        mParticlePipeline.emplace(*mCtx, *mSwapchain, *mCmds, *mScene.particles);
        mCmds->allocateComputeCommandBuffers(*mCtx);
    }

    mSync.emplace(*mCtx, static_cast<uint32_t>(mSwapchain->images.size()),
            mScene.particles.has_value());

    buildRenderGraph();
}

void Renderer::createSceneObjects() {
    for (uint32_t i = 0; i < mScene.meshInstances.size(); ++i) {
        auto const& instance = mScene.meshInstances[i];
        GameObject object;
        object.position = instance.position;
        object.rotation = glm::radians(instance.rotation);
        object.scale = instance.scale;
        mGameObjects.push_back(std::move(object));

        RenderObject renderObject;
        renderObject.gameObjectIndex = i;
        mRenderObjects.push_back(std::move(renderObject));
    }
    std::cout << "Game objects: " << mGameObjects.size() << " created\n";
}

void Renderer::createEnvironment() {
    if (mScene.iblPath) {
        auto iblName = std::filesystem::path(*mScene.iblPath).filename().string();
        mIblEnvironment.emplace(*mCtx, *mCmds, *mScene.iblPath + "/" + iblName + "_ibl.ktx",
                *mScene.iblPath + "/sh.txt");
        if (mScene.skybox) {
            mSkyboxPipeline.emplace(*mCtx, *mSwapchain, *mIblEnvironment);
        }
    }
}

void Renderer::createShadowPipeline() {
    DirectionalLight const* shadowCastingLight = nullptr;
    for (auto const& light: mScene.lights) {
        if (auto const* directional = std::get_if<DirectionalLight>(&light)) {
            if (directional->castShadow) {
                shadowCastingLight = directional;
                break;
            }
        }
    }
    if (shadowCastingLight) {
        uint32_t shadowCasterCount = static_cast<uint32_t>(
                std::count_if(mScene.meshInstances.begin(), mScene.meshInstances.end(),
                        [](MeshInstance const& inst) { return inst.castShadows; }));
        mShadowPipeline.emplace(*mCtx, *mCmds, shadowCasterCount);
        mShadowPipeline->shadowType = shadowCastingLight->shadowType;
        mShadowPipeline->shadowBias = shadowCastingLight->shadowBias;
        mShadowPipeline->updateLightSpaceMatrix(*shadowCastingLight);
    }
}

void Renderer::createAoPipeline() {
    if (mScene.sao) {
        mAoPipeline.emplace(*mCtx, *mSwapchain, *mScene.sao);
    } else if (mScene.gtao) {
        mAoPipeline.emplace(*mCtx, *mSwapchain, *mScene.gtao);
    }
    if (mAoPipeline) {
        mAoPipeline->allocateNormalsObjects(*mCtx,
                static_cast<uint32_t>(mScene.meshInstances.size()));
    }
}

void Renderer::createMaterials() {
    for (auto const& instance: mScene.meshInstances) {
        bool doubleSided = mMeshBuffer->meshRanges.at(instance.gltfPath).doubleSided;
        auto key = materialKey(instance, doubleSided);
        if (!mMaterials.contains(key)) {
            mMaterials.emplace(key,
                    Material(*mCtx, *mSwapchain, instance.vertexShader, instance.resolvedFragShader,
                            instance.shaderFeatures, doubleSided));
        }
    }
}

void Renderer::createRenderObjects() {
    for (auto& renderObject: mRenderObjects) {
        auto const& instance = mScene.meshInstances[renderObject.gameObjectIndex];
        renderObject.range = mMeshBuffer->meshRanges.at(instance.gltfPath);
        renderObject.texture = &mResources->getTexture(*mCtx, *mCmds, instance.texturePath,
                instance.gltfPath, &*mMeshBuffer);
        renderObject.normalMap =
                &mResources->getNormalMap(*mCtx, *mCmds, instance.gltfPath, *mMeshBuffer);
        auto key = materialKey(instance, renderObject.range.doubleSided);
        renderObject.material = &mMaterials.at(key);
        renderObject.materialInstance = renderObject.material->createInstance(*mCtx,
                *renderObject.texture, mLightBuffer->buffer,
                hasFeature(instance.shaderFeatures, ShaderFeatures::Ibl) ? &*mIblEnvironment
                                                                         : nullptr,
                hasFeature(instance.shaderFeatures, ShaderFeatures::NormalMap)
                        ? renderObject.normalMap
                        : nullptr,
                (hasFeature(instance.shaderFeatures, ShaderFeatures::HardShadow) ||
                        hasFeature(instance.shaderFeatures, ShaderFeatures::PcfShadow))
                        ? &*mShadowPipeline
                        : nullptr,
                hasFeature(instance.shaderFeatures, ShaderFeatures::Ao) ? &*mAoPipeline : nullptr);
    }
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
    glm::mat4 projection = mCamera.projMatrix(aspect);

    // Lights are static but the UBO is re-uploaded every frame for simplicity.
    // exposure = 1 / (1.2 × aperture² / shutter × 100 / ISO)  (matches Filament's Exposure.cpp)
    float exposure =
            1.0f / (1.2f * static_cast<float>(mScene.camera.aperture * mScene.camera.aperture /
                                              mScene.camera.shutterSpeed * 100.0 /
                                              mScene.camera.sensitivity));

    updateLights(exposure);
    updateMaterialUniforms(view, projection, mCamera.position(), exposure);

    if (mSkyboxPipeline) {
        SkyboxUBO skyboxUbo{
            .invProj = glm::inverse(projection),
            .invView = glm::inverse(view),
        };
        mSkyboxPipeline->updateUBO(mFrameIndex, skyboxUbo);
    }

    updateShadowUniforms();

    if (mScene.particles) {
        ComputeUBO cubo{ .deltaTime = deltaTime };
        memcpy(mParticlePipeline->computeUniformBuffersMapped[mFrameIndex], &cubo, sizeof(cubo));
    }

    updateAoUniforms(view, projection);
}

void Renderer::updateLights(float exposure) {
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
}

void Renderer::updateMaterialUniforms(glm::mat4 const& view, glm::mat4 const& projection,
        glm::vec3 const& cameraPosition, float exposure) {
    float iblLuminance = diffuseIblLuminance(mScene.lights, exposure);
    for (auto& renderObject: mRenderObjects) {
        auto const& object = mGameObjects[renderObject.gameObjectIndex];
        glm::mat4 model = object.getModelMatrix();
        auto const& instance = mScene.meshInstances[renderObject.gameObjectIndex];

        UniformBufferObject ubo{
            .model = model,
            .view = view,
            .proj = projection,
            .normalMatrix = glm::transpose(glm::inverse(model)),
            .baseColor = instance.baseColor,
            .cameraPos = glm::vec4(cameraPosition, 0.0f),
            .pbrParams = glm::vec4(instance.metallic, instance.roughness, iblLuminance, 0.0f),
        };
        renderObject.materialInstance.updateUBO(mFrameIndex, ubo);
    }
}

void Renderer::updateShadowUniforms() {
    if (mShadowPipeline) {
        uint32_t shadowObjIdx = 0;
        for (size_t i = 0; i < mScene.meshInstances.size(); ++i) {
            if (mScene.meshInstances[i].castShadows) {
                mShadowPipeline->updateObjectUBO(shadowObjIdx++, mFrameIndex,
                        mGameObjects[i].getModelMatrix());
            }
        }
        mShadowPipeline->updateFragmentUBO(mFrameIndex);
    }
}

void Renderer::updateAoUniforms(glm::mat4 const& view, glm::mat4 const& projection) {
    if (mAoPipeline) {
        float fovYRad = static_cast<float>(glm::radians(mScene.camera.fovDegrees));
        mAoPipeline->updateUBOs(mFrameIndex, view, projection, fovYRad,
                static_cast<float>(mSwapchain->extent.height),
                static_cast<float>(mCamera.nearPlane), static_cast<float>(mCamera.farPlane));

        for (size_t i = 0; i < mRenderObjects.size(); ++i) {
            auto const& obj = mGameObjects[mRenderObjects[i].gameObjectIndex];
            glm::mat4 model = obj.getModelMatrix();
            NormalsUBO nubo{
                .model = model,
                .view = view,
                .proj = projection,
                .normalMatrix = glm::transpose(glm::inverse(model)),
            };
            mAoPipeline->updateNormalsUBO(static_cast<uint32_t>(i), mFrameIndex, nubo);
        }
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

    if (mShadowPipeline) {
        captureShadowMapDebug();
    }
    if (mAoPipeline) {
        captureAoTextureDebug();
        captureNormalsTextureDebug();
    }
    if (mExitAfterScreenshot) {
        glfwSetWindowShouldClose(mWindow, GLFW_TRUE);
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

void Renderer::captureImageToPng(vk::Image image, vk::ImageLayout currentLayout,
        vk::ImageAspectFlags aspect, uint32_t width, uint32_t height, uint32_t bytesPerPixel,
        std::string const& outputPath, PixelTransform transform) {
    uint32_t pixelCount = width * height;
    vk::DeviceSize readbackBufferSize = vk::DeviceSize(pixelCount) * bytesPerPixel;

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

    vkutil::transitionImageLayout(cmd, image, currentLayout, vk::ImageLayout::eTransferSrcOptimal,
            vk::AccessFlagBits2::eShaderRead, vk::AccessFlagBits2::eTransferRead,
            vk::PipelineStageFlagBits2::eFragmentShader, vk::PipelineStageFlagBits2::eTransfer,
            aspect);

    vk::BufferImageCopy copyRegion{
        .imageSubresource = { .aspectMask = aspect,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1 },
        .imageExtent = { width, height, 1 },
    };
    cmd.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, *readbackBuffer, copyRegion);

    vkutil::transitionImageLayout(cmd, image, vk::ImageLayout::eTransferSrcOptimal, currentLayout,
            vk::AccessFlagBits2::eTransferRead, vk::AccessFlagBits2::eShaderRead,
            vk::PipelineStageFlagBits2::eTransfer, vk::PipelineStageFlagBits2::eFragmentShader,
            aspect);

    cmd.end();

    vk::raii::Fence fence(mCtx->device, vk::FenceCreateInfo{});
    vk::CommandBuffer cmdHandle = *cmd;
    mCtx->graphicsQueue.submit(
            vk::SubmitInfo{ .commandBufferCount = 1, .pCommandBuffers = &cmdHandle }, *fence);
    if (mCtx->device.waitForFences(*fence, vk::True, std::numeric_limits<uint64_t>::max()) !=
            vk::Result::eSuccess) {
        throw std::runtime_error("captureImageToPng: fence wait failed");
    }

    void* mapped = readbackMemory.mapMemory(0, readbackBufferSize);
    mCtx->device.invalidateMappedMemoryRanges(vk::MappedMemoryRange{ .memory = *readbackMemory,
        .offset = 0,
        .size = readbackBufferSize });

    std::vector<uint8_t> rgba;
    if (transform) {
        rgba = transform(mapped, pixelCount);
    } else {
        rgba.assign(static_cast<uint8_t const*>(mapped),
                static_cast<uint8_t const*>(mapped) + pixelCount * 4);
    }
    readbackMemory.unmapMemory();

    stbi_write_png(outputPath.c_str(), static_cast<int>(width), static_cast<int>(height), 4,
            rgba.data(), static_cast<int>(width) * 4);
    std::cout << outputPath << " saved\n";
}

void Renderer::captureShadowMapDebug() {
    constexpr uint32_t shadowMapSize = SHADOW_MAP_SIZE;
    std::string path = screenshotOutputPath("shadow_depth.png");
    captureImageToPng(*mShadowPipeline->image, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eDepth, shadowMapSize, shadowMapSize, sizeof(float), path,
            [](void const* data, uint32_t pixelCount) {
                return depthFloatsToGrayscaleRgba(static_cast<float const*>(data), pixelCount);
            });
}

void Renderer::captureAoTextureDebug() {
    uint32_t width = mSwapchain->extent.width;
    uint32_t height = mSwapchain->extent.height;
    std::string path = screenshotOutputPath(mScene.gtao ? "gtao_texture.png" : "ao_texture.png");
    // Extract R (AO value) and expand to grayscale RGBA
    captureImageToPng(*mAoPipeline->aoRawImage, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor, width, height, 4, path,
            [](void const* data, uint32_t pixelCount) {
                auto* src = static_cast<uint8_t const*>(data);
                std::vector<uint8_t> rgba(pixelCount * 4);
                for (uint32_t i = 0; i < pixelCount; ++i) {
                    uint8_t ao = src[i * 4];
                    rgba[i * 4 + 0] = ao;
                    rgba[i * 4 + 1] = ao;
                    rgba[i * 4 + 2] = ao;
                    rgba[i * 4 + 3] = 255;
                }
                return rgba;
            });
}

void Renderer::captureNormalsTextureDebug() {
    uint32_t width = mSwapchain->extent.width;
    uint32_t height = mSwapchain->extent.height;
    std::string path = screenshotOutputPath("normals_texture.png");
    captureImageToPng(*mAoPipeline->normalsImage, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor, width, height, 4, path);
}

std::string Renderer::screenshotOutputPath(std::string const& filename) const {
    auto slash = mScreenshotPath.rfind('/');
    return (slash != std::string::npos ? mScreenshotPath.substr(0, slash + 1) : "") + filename;
}

void Renderer::resolveShaderVariants() {
    for (auto& inst: mScene.meshInstances) {
        if (inst.fragmentShader != "pbr") {
            inst.resolvedFragShader = inst.fragmentShader;
            continue;
        }

        bool hasNormalMap = inst.useNormalMap && mMeshBuffer->normalMaps.contains(inst.gltfPath);
        bool hasShadow = inst.receiveShadows && mShadowPipeline.has_value();
        bool hasPcf = hasShadow && mShadowPipeline->shadowType == ShadowType::PCF;
        bool hasAo = mAoPipeline.has_value();

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
        if (hasAo) {
            // The PBR AO sampler/variant is shared by SAO and GTAO.
            features = features | ShaderFeatures::Ao;
            frag += "_sao";
        }

        inst.resolvedFragShader = frag;
        inst.shaderFeatures = features;
    }
}

void Renderer::buildRenderGraph() {
    mRenderGraph = RenderGraph{};
    mShadowMapImageHandle = {};
    mNormalsImageHandle = {};
    mDepthPrepassImageHandle = {};
    mDepthMipImageHandles.clear();
    mAoRawImageHandle = {};
    mAoBlurImageHandle = {};

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

    if (mShadowPipeline) {
        vk::Extent2D shadowExtent{ SHADOW_MAP_SIZE, SHADOW_MAP_SIZE };
        mShadowMapImageHandle = mRenderGraph.importImage("shadowMap", *mShadowPipeline->image,
                *mShadowPipeline->imageView,
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
                    setViewportAndScissor(cmd, { SHADOW_MAP_SIZE, SHADOW_MAP_SIZE });
                    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *mShadowPipeline->pipeline);
                    cmd.bindVertexBuffers(0, *mMeshBuffer->vertexBuffer, { vk::DeviceSize{ 0 } });
                    cmd.bindIndexBuffer(*mMeshBuffer->indexBuffer, 0, vk::IndexType::eUint32);
                    uint32_t shadowObjIdx = 0;
                    for (size_t i = 0; i < mRenderObjects.size(); ++i) {
                        auto const& inst = mScene.meshInstances[i];
                        if (!inst.castShadows) {
                            continue;
                        }
                        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                *mShadowPipeline->pipelineLayout, 0,
                                *mShadowPipeline->objects[shadowObjIdx].descriptorSets[mFrameIndex],
                                {});
                        cmd.drawIndexed(mRenderObjects[i].range.indexCount, 1,
                                mRenderObjects[i].range.firstIndex, 0, 0);
                        ++shadowObjIdx;
                    }
                });
    }

    if (mAoPipeline) {
        vk::Format depthFmt = vkutil::findDepthFormat(*mCtx);

        mNormalsImageHandle = mRenderGraph.importImage("normalsPrepass", *mAoPipeline->normalsImage,
                *mAoPipeline->normalsView,
                RenderGraphImage{
                    .format = vk::Format::eR8G8B8A8Unorm,
                    .extent = swapchainExtent,
                    .usage = vk::ImageUsageFlagBits::eColorAttachment |
                             vk::ImageUsageFlagBits::eSampled,
                    .aspect = vk::ImageAspectFlagBits::eColor,
                    .samples = vk::SampleCountFlagBits::e1,
                });

        mDepthPrepassImageHandle = mRenderGraph.importImage("depthPrepass",
                *mAoPipeline->depthImage, *mAoPipeline->depthMipViews[0],
                RenderGraphImage{
                    .format = depthFmt,
                    .extent = swapchainExtent,
                    .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment |
                             vk::ImageUsageFlagBits::eSampled,
                    .aspect = vk::ImageAspectFlagBits::eDepth,
                    .samples = vk::SampleCountFlagBits::e1,
                });

        mAoRawImageHandle =
                mRenderGraph.importImage("aoRaw", *mAoPipeline->aoRawImage, *mAoPipeline->aoRawView,
                        RenderGraphImage{
                            .format = vk::Format::eR8Unorm,
                            .extent = swapchainExtent,
                            .usage = vk::ImageUsageFlagBits::eColorAttachment |
                                     vk::ImageUsageFlagBits::eSampled |
                                     vk::ImageUsageFlagBits::eTransferSrc,
                            .aspect = vk::ImageAspectFlagBits::eColor,
                            .samples = vk::SampleCountFlagBits::e1,
                        });

        mAoBlurImageHandle = mRenderGraph.importImage("aoBlur", *mAoPipeline->aoBlurImage,
                *mAoPipeline->aoBlurView,
                RenderGraphImage{
                    .format = vk::Format::eR8Unorm,
                    .extent = swapchainExtent,
                    .usage = vk::ImageUsageFlagBits::eColorAttachment |
                             vk::ImageUsageFlagBits::eSampled,
                    .aspect = vk::ImageAspectFlagBits::eColor,
                    .samples = vk::SampleCountFlagBits::e1,
                });

        mRenderGraph.addPass("NormalsPrepass")
                .writesColor(mNormalsImageHandle)
                .writesDepth(mDepthPrepassImageHandle)
                .execute([this](vk::raii::CommandBuffer const& cmd) {
                    setViewportAndScissor(cmd, mSwapchain->extent);
                    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                            *mAoPipeline->normalsPipeline);
                    cmd.bindVertexBuffers(0, *mMeshBuffer->vertexBuffer, { vk::DeviceSize{ 0 } });
                    cmd.bindIndexBuffer(*mMeshBuffer->indexBuffer, 0, vk::IndexType::eUint32);
                    for (size_t i = 0; i < mRenderObjects.size(); ++i) {
                        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                *mAoPipeline->normalsPipeLayout, 0,
                                *mAoPipeline->normalsObjects[i].descriptorSets[mFrameIndex], {});
                        cmd.drawIndexed(mRenderObjects[i].range.indexCount, 1,
                                mRenderObjects[i].range.firstIndex, 0, 0);
                    }
                });

        if (mScene.sao) {
            mDepthMipImageHandles.push_back(mDepthPrepassImageHandle);
            for (uint32_t level = 1; level < mAoPipeline->depthMipLevelCount; ++level) {
                vk::Extent2D mipExtent{
                    std::max(1u, swapchainExtent.width >> level),
                    std::max(1u, swapchainExtent.height >> level),
                };
                auto mipHandle = mRenderGraph.importImage("depthMip" + std::to_string(level),
                        *mAoPipeline->depthImage, *mAoPipeline->depthMipViews[level],
                        RenderGraphImage{
                            .format = depthFmt,
                            .extent = mipExtent,
                            .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment |
                                     vk::ImageUsageFlagBits::eSampled,
                            .aspect = vk::ImageAspectFlagBits::eDepth,
                            .samples = vk::SampleCountFlagBits::e1,
                            .mipLevel = level,
                        });
                mDepthMipImageHandles.push_back(mipHandle);
                mRenderGraph.addPass("DepthMip" + std::to_string(level))
                        .reads(mDepthMipImageHandles[level - 1])
                        .writesDepth(mipHandle)
                        .execute([this, level, mipExtent](vk::raii::CommandBuffer const& cmd) {
                            setViewportAndScissor(cmd, mipExtent);
                            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                    *mAoPipeline->depthMipPipeline);
                            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                    *mAoPipeline->depthMipPipeLayout, 0,
                                    *mAoPipeline->depthMipDescSets[level - 1], {});
                            cmd.draw(3, 1, 0, 0);
                        });
            }
        }

        mRenderGraph.addPass(mScene.gtao ? "GtaoPass" : "SaoPass")
                .writesColor(mAoRawImageHandle)
                .reads(mDepthPrepassImageHandle)
                .reads(mNormalsImageHandle)
                .execute([this](vk::raii::CommandBuffer const& cmd) {
                    setViewportAndScissor(cmd, mSwapchain->extent);
                    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *mAoPipeline->aoPipeline);
                    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            *mAoPipeline->aoPipeLayout, 0, *mAoPipeline->aoDescSets[mFrameIndex],
                            {});
                    cmd.draw(3, 1, 0, 0);
                });

        // The SAO shader samples every mip through the full-range depth view.
        if (mScene.sao) {
            for (size_t level = 1; level < mDepthMipImageHandles.size(); ++level) {
                mRenderGraph.reads(mDepthMipImageHandles[level]);
            }
        }

        mRenderGraph.addPass("BlurH")
                .writesColor(mAoBlurImageHandle)
                .reads(mAoRawImageHandle)
                .execute([this](vk::raii::CommandBuffer const& cmd) {
                    setViewportAndScissor(cmd, mSwapchain->extent);
                    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *mAoPipeline->blurPipeline);
                    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            *mAoPipeline->blurPipeLayout, 0,
                            *mAoPipeline->blurDescSets[mFrameIndex][0], {});
                    cmd.draw(3, 1, 0, 0);
                });

        mRenderGraph.addPass("BlurV")
                .writesColor(mAoRawImageHandle)
                .reads(mAoBlurImageHandle)
                .execute([this](vk::raii::CommandBuffer const& cmd) {
                    setViewportAndScissor(cmd, mSwapchain->extent);
                    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *mAoPipeline->blurPipeline);
                    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            *mAoPipeline->blurPipeLayout, 0,
                            *mAoPipeline->blurDescSets[mFrameIndex][1], {});
                    cmd.draw(3, 1, 0, 0);
                });
    }

    auto& forwardPass = mRenderGraph.addPass("ForwardPass")
                                .writesColor(colorImage)
                                .resolvesTo(mSwapchainImageHandle)
                                .writesDepth(depthImage);
    if (mShadowMapImageHandle.isValid()) {
        forwardPass.reads(mShadowMapImageHandle);
    }
    if (mAoRawImageHandle.isValid()) {
        forwardPass.reads(mAoRawImageHandle);
    }
    forwardPass.execute([this](vk::raii::CommandBuffer const& commandBuffer) {
        commandBuffer.bindVertexBuffers(0, *mMeshBuffer->vertexBuffer, { vk::DeviceSize{ 0 } });
        commandBuffer.bindIndexBuffer(*mMeshBuffer->indexBuffer, 0, vk::IndexType::eUint32);
        setViewportAndScissor(commandBuffer, mSwapchain->extent);

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
