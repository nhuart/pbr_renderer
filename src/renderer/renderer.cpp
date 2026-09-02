#include "renderer/renderer.hpp"
#include "core/resource_allocator.hpp"

#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

Renderer::Renderer(std::string scenePath)
        : mScenePath(std::move(scenePath)) {}

void Renderer::run() {
    mScene = loadScene(mScenePath);
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
}

void Renderer::initVulkan() {
    mCtx.emplace(mWindow);
    mSwapchain.emplace(*mCtx, mWindow);
    mCmds.emplace(*mCtx);
    mCmds->allocateCommandBuffers(*mCtx);

    for (auto const& inst: mScene.meshInstances) {
        GameObject obj;
        obj.position = inst.position;
        obj.rotation = glm::radians(inst.rotation);
        obj.scale = inst.scale;
        mGameObjects.push_back(std::move(obj));
    }
    std::cout << "Game objects: " << mGameObjects.size() << " created\n";

    mTexture.emplace(*mCtx, *mCmds, mScene.meshInstances.front().texturePath);
    mMeshBuffer.emplace(*mCtx, *mCmds, mScene);

    for (auto& obj: mGameObjects) {
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            auto [buf, mem] = vkutil::createBuffer(*mCtx, sizeof(UniformBufferObject),
                    vk::BufferUsageFlagBits::eUniformBuffer,
                    vk::MemoryPropertyFlagBits::eHostVisible |
                            vk::MemoryPropertyFlagBits::eHostCoherent);
            obj.uniformBuffersMapped.push_back(mem.mapMemory(0, sizeof(UniformBufferObject)));
            obj.uniformBuffers.push_back(std::move(buf));
            obj.uniformBuffersMemory.push_back(std::move(mem));
        }
    }

    mMeshPipeline.emplace(*mCtx, *mSwapchain);
    mMeshPipeline->allocateDescriptorSets(*mCtx, mGameObjects, *mTexture->sampler,
            *mTexture->imageView);

    if (mScene.particles) {
        mParticlePipeline.emplace(*mCtx, *mSwapchain, *mCmds, *mScene.particles);
        mCmds->allocateComputeCommandBuffers(*mCtx);
    }

    mSync.emplace(*mCtx, static_cast<uint32_t>(mSwapchain->images.size()),
            mScene.particles.has_value());
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

void Renderer::recreateSwapchain() {
    mCtx->device.waitIdle();
    mSwapchain->recreate(*mCtx, mWindow);
    mSync->recreatePresent(*mCtx, static_cast<uint32_t>(mSwapchain->images.size()));
}

void Renderer::updateUniforms() {
    static auto startTime = std::chrono::high_resolution_clock::now();
    static auto lastTime = startTime;
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float>(currentTime - startTime).count();
    float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count() * 1000.0f;
    lastTime = currentTime;

    glm::mat4 view =
            glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f),
            static_cast<float>(mSwapchain->extent.width) /
                    static_cast<float>(mSwapchain->extent.height),
            0.1f, 10.0f);
    proj[1][1] *= -1;

    for (auto& obj: mGameObjects) {
        obj.rotation.z = time * glm::radians(15.0f);
        UniformBufferObject ubo{
            .model = obj.getModelMatrix(),
            .view = view,
            .proj = proj,
        };
        memcpy(obj.uniformBuffersMapped[mFrameIndex], &ubo, sizeof(ubo));
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

void Renderer::recordCommandBuffer(uint32_t imageIndex) {
    auto const& cmd = mCmds->commandBuffers[mFrameIndex];
    cmd.begin({});

    vkutil::transitionImageLayout(cmd, mSwapchain->images[imageIndex], vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, {},
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput);

    vkutil::transitionImageLayout(cmd, *mSwapchain->colorImage, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, {},
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput);

    vkutil::transitionImageLayout(cmd, *mSwapchain->depthImage, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                    vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                    vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::ImageAspectFlagBits::eDepth);

    vk::ClearValue clearColor = vk::ClearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f };
    vk::RenderingAttachmentInfo colorAttachmentInfo{
        .imageView = *mSwapchain->colorImageView,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .resolveMode = vk::ResolveModeFlagBits::eAverage,
        .resolveImageView = *mSwapchain->imageViews[imageIndex],
        .resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = clearColor,
    };
    vk::ClearValue clearDepth = vk::ClearDepthStencilValue{ 1.0f, 0 };
    vk::RenderingAttachmentInfo depthAttachmentInfo{
        .imageView = *mSwapchain->depthImageView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = clearDepth,
    };
    vk::RenderingInfo renderingInfo{
        .renderArea = { .offset = { 0, 0 }, .extent = mSwapchain->extent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentInfo,
        .pDepthAttachment = &depthAttachmentInfo,
    };

    cmd.beginRendering(renderingInfo);

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *mMeshPipeline->pipeline);
    cmd.bindVertexBuffers(0, *mMeshBuffer->vertexBuffer, { vk::DeviceSize{ 0 } });
    cmd.bindIndexBuffer(*mMeshBuffer->indexBuffer, 0, vk::IndexType::eUint32);

    vk::Viewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(mSwapchain->extent.width),
        .height = static_cast<float>(mSwapchain->extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, vk::Rect2D{ .offset = { 0, 0 }, .extent = mSwapchain->extent });

    for (auto const& obj: mGameObjects) {
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *mMeshPipeline->pipelineLayout, 0,
                *obj.descriptorSets[mFrameIndex], {});
        cmd.drawIndexed(static_cast<uint32_t>(mMeshBuffer->indices.size()), 1, 0, 0, 0);
    }

    if (mScene.particles) {
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *mParticlePipeline->particlePipeline);
        cmd.bindVertexBuffers(0, *mParticlePipeline->shaderStorageBuffers[mFrameIndex],
                { vk::DeviceSize{ 0 } });
        cmd.draw(mScene.particles->count, 1, 0, 0);
    }

    cmd.endRendering();

    vkutil::transitionImageLayout(cmd, mSwapchain->images[imageIndex],
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite, {},
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eBottomOfPipe);

    cmd.end();
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
    if (presentResult == vk::Result::eErrorOutOfDateKHR ||
            presentResult == vk::Result::eSuboptimalKHR || mFramebufferResized) {
        mFramebufferResized = false;
        recreateSwapchain();
    } else if (presentResult != vk::Result::eSuccess) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    mFrameIndex = (mFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}
