#include "renderer/renderer.hpp"
#include "core/resource_allocator.hpp"

#include <chrono>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

#include <stb_image_write.h>


#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

Renderer::Renderer(std::string scenePath, std::string screenshotPath)
        : mScenePath(std::move(scenePath))
        , mScreenshotPath(std::move(screenshotPath)) {}

void Renderer::run() {
    mScene = loadScene(mScenePath);
    mCamera = *mScene.camera;
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

    for (auto const& inst: mScene.meshInstances) {
        auto key = inst.vertexShader + "+" + inst.fragmentShader;
        if (!mMaterials.contains(key))
            mMaterials.emplace(key, Material(*mCtx, *mSwapchain, inst.vertexShader, inst.fragmentShader));
    }

    for (auto& ro: mRenderObjects) {
        auto const& inst = mScene.meshInstances[ro.gameObjectIndex];
        ro.texture = &mResources->getTexture(*mCtx, *mCmds, inst.texturePath);
        ro.material = &mMaterials.at(inst.vertexShader + "+" + inst.fragmentShader);
        ro.materialInstance = ro.material->createInstance(*mCtx, *ro.texture);
    }
    mMeshBuffer.emplace(*mCtx, *mCmds, mScene);

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

    for (auto& ro: mRenderObjects) {
        auto const& obj = mGameObjects[ro.gameObjectIndex];
        glm::vec3 camPos = mCamera.position();
        glm::mat4 model = obj.getModelMatrix();
        auto const& meshInst = mScene.meshInstances[ro.gameObjectIndex];

        UniformBufferObject ubo{
            .model = model,
            .view = view,
            .proj = proj,
            .normalMatrix = glm::transpose(glm::inverse(model)),
            .baseColor = meshInst.baseColor,
            .cameraPos = glm::vec4(camPos, 0.0f),
            .pbrParams = glm::vec4(meshInst.metallic, meshInst.roughness, 0.0f, 0.0f),
        };
        ro.materialInstance.updateUBO(mFrameIndex, ubo);
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
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    vk::raii::CommandBuffer cmd = std::move(mCtx->device
            .allocateCommandBuffers(vk::CommandBufferAllocateInfo{
                .commandPool = *mCmds->commandPool,
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = 1,
            })
            .front());

    cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

    vk::Image srcImage = mSwapchain->images[imageIndex];

    vkutil::transitionImageLayout(cmd, srcImage,
            vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eTransferSrcOptimal,
            vk::AccessFlagBits2::eNone, vk::AccessFlagBits2::eTransferRead,
            vk::PipelineStageFlagBits2::eBottomOfPipe, vk::PipelineStageFlagBits2::eTransfer);

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

    vkutil::transitionImageLayout(cmd, srcImage,
            vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eTransferRead, vk::AccessFlagBits2::eNone,
            vk::PipelineStageFlagBits2::eTransfer, vk::PipelineStageFlagBits2::eBottomOfPipe);

    cmd.end();

    vk::raii::Fence fence(mCtx->device, vk::FenceCreateInfo{});
    vk::CommandBuffer cmdHandle = *cmd;
    mCtx->graphicsQueue.submit(vk::SubmitInfo{
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdHandle,
    }, *fence);

    std::ignore = mCtx->device.waitForFences(*fence, vk::True, std::numeric_limits<uint64_t>::max());

    // Copy pixels out of the mapped buffer into a plain vector
    void* mapped = readbackMemory.mapMemory(0, bufferSize);
    auto* src = static_cast<uint8_t*>(mapped);
    std::vector<uint8_t> rgba(bufferSize);
    for (uint32_t i = 0; i < width * height; ++i) {
        rgba[i * 4 + 0] = src[i * 4 + 2]; // R <- B  (swapchain is BGRA)
        rgba[i * 4 + 1] = src[i * 4 + 1]; // G
        rgba[i * 4 + 2] = src[i * 4 + 0]; // B <- R
        rgba[i * 4 + 3] = 255;
    }
    readbackMemory.unmapMemory();

    stbi_write_png(mScreenshotPath.c_str(), static_cast<int>(width), static_cast<int>(height), 4,
            rgba.data(), static_cast<int>(width) * 4);
    std::cout << "Screenshot saved: " << mScreenshotPath << "\n";
}

void Renderer::buildRenderGraph() {
    mRenderGraph = RenderGraph{};

    vk::Extent2D swapchainExtent = mSwapchain->extent;

    // Import swapchain-provided images — the graph records transitions but does not own them.
    // updateImportedImage() updates the swapchain backing each frame.
    mSwapchainImageHandle = mRenderGraph.importImage("swapchain", mSwapchain->images[0],
            *mSwapchain->imageViews[0],
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

    mRenderGraph.addPass("ForwardPass")
            .writesColor(colorImage)
            .resolvesTo(mSwapchainImageHandle)
            .writesDepth(depthImage)
            .execute([this](vk::raii::CommandBuffer const& commandBuffer) {
                vk::Extent2D drawExtent = mSwapchain->extent;
                commandBuffer.bindVertexBuffers(0, *mMeshBuffer->vertexBuffer,
                        { vk::DeviceSize{ 0 } });
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
                    commandBuffer.drawIndexed(static_cast<uint32_t>(mMeshBuffer->indices.size()), 1,
                            0, 0, 0);
                }

                if (mScene.particles) {
                    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                            *mParticlePipeline->particlePipeline);
                    commandBuffer.bindVertexBuffers(0,
                            *mParticlePipeline->shaderStorageBuffers[mFrameIndex],
                            { vk::DeviceSize{ 0 } });
                    commandBuffer.draw(mScene.particles->count, 1, 0, 0);
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

    if (!mScreenshotPath.empty() && presentResult == vk::Result::eSuccess) {
        captureScreenshot(imageIndex);
        mScreenshotPath.clear();
    }

    if (presentResult == vk::Result::eErrorOutOfDateKHR ||
            presentResult == vk::Result::eSuboptimalKHR || mFramebufferResized) {
        mFramebufferResized = false;
        recreateSwapchain();
    } else if (presentResult != vk::Result::eSuccess) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    mFrameIndex = (mFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}
