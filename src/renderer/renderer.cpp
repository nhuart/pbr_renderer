#include "renderer/renderer.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

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
