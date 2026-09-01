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
        : scenePath_(std::move(scenePath)) {}

void Renderer::run() {
    scene_ = loadScene(scenePath_);
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
}

void Renderer::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window_ = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebufferResizeCallback);
}

void Renderer::initVulkan() {
    ctx_.emplace(window_);
    swapchain_.emplace(*ctx_, window_);
    cmds_.emplace(*ctx_);
    cmds_->allocateCommandBuffers(*ctx_);

    for (auto const& inst: scene_.meshInstances) {
        GameObject obj;
        obj.position = inst.position;
        obj.rotation = glm::radians(inst.rotation);
        obj.scale = inst.scale;
        gameObjects_.push_back(std::move(obj));
    }
    std::cout << "Game objects: " << gameObjects_.size() << " created\n";

    texture_.emplace(*ctx_, *cmds_, scene_.meshInstances.front().texturePath);
    meshBuffer_.emplace(*ctx_, *cmds_, scene_);

    for (auto& obj: gameObjects_) {
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            auto [buf, mem] = vkutil::createBuffer(*ctx_, sizeof(UniformBufferObject),
                    vk::BufferUsageFlagBits::eUniformBuffer,
                    vk::MemoryPropertyFlagBits::eHostVisible |
                            vk::MemoryPropertyFlagBits::eHostCoherent);
            obj.uniformBuffersMapped.push_back(mem.mapMemory(0, sizeof(UniformBufferObject)));
            obj.uniformBuffers.push_back(std::move(buf));
            obj.uniformBuffersMemory.push_back(std::move(mem));
        }
    }

    meshPipeline_.emplace(*ctx_, *swapchain_);
    meshPipeline_->allocateDescriptorSets(*ctx_, gameObjects_, *texture_->sampler,
            *texture_->imageView);

    if (scene_.particles) {
        particlePipeline_.emplace(*ctx_, *swapchain_, *cmds_, *scene_.particles);
        cmds_->allocateComputeCommandBuffers(*ctx_);
    }

    sync_.emplace(*ctx_, static_cast<uint32_t>(swapchain_->images.size()),
            scene_.particles.has_value());
}

void Renderer::mainLoop() {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        drawFrame();
    }
    ctx_->device.waitIdle();
}

void Renderer::cleanup() {
    glfwDestroyWindow(window_);
    glfwTerminate();
}

void Renderer::framebufferResizeCallback(GLFWwindow* window, int /*width*/, int /*height*/) {
    auto* app = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    app->framebufferResized_ = true;
}

void Renderer::recreateSwapchain() {
    ctx_->device.waitIdle();
    swapchain_->recreate(*ctx_, window_);
    sync_->recreatePresent(*ctx_, static_cast<uint32_t>(swapchain_->images.size()));
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
            static_cast<float>(swapchain_->extent.width) /
                    static_cast<float>(swapchain_->extent.height),
            0.1f, 10.0f);
    proj[1][1] *= -1;

    for (auto& obj: gameObjects_) {
        obj.rotation.z = time * glm::radians(15.0f);
        UniformBufferObject ubo{
            .model = obj.getModelMatrix(),
            .view = view,
            .proj = proj,
        };
        memcpy(obj.uniformBuffersMapped[frameIndex_], &ubo, sizeof(ubo));
    }

    if (scene_.particles) {
        ComputeUBO cubo{ .deltaTime = deltaTime };
        memcpy(particlePipeline_->computeUniformBuffersMapped[frameIndex_], &cubo, sizeof(cubo));
    }
}

void Renderer::recordComputeCommandBuffer(uint32_t frameIdx) {
    auto const& cmd = cmds_->computeCommandBuffers[frameIdx];
    cmd.begin({});
    if (scene_.particles) {
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *particlePipeline_->computePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                *particlePipeline_->computePipelineLayout, 0,
                *particlePipeline_->computeDescriptorSets[frameIdx], {});
        cmd.dispatch(scene_.particles->count / 256, 1, 1);
    }
    cmd.end();
}

void Renderer::recordCommandBuffer(uint32_t imageIndex) {
    auto const& cmd = cmds_->commandBuffers[frameIndex_];
    cmd.begin({});

    vkutil::transitionImageLayout(cmd, swapchain_->images[imageIndex], vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, {},
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput);

    vkutil::transitionImageLayout(cmd, *swapchain_->colorImage, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, {},
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput);

    vkutil::transitionImageLayout(cmd, *swapchain_->depthImage, vk::ImageLayout::eUndefined,
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
        .imageView = *swapchain_->colorImageView,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .resolveMode = vk::ResolveModeFlagBits::eAverage,
        .resolveImageView = *swapchain_->imageViews[imageIndex],
        .resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = clearColor,
    };
    vk::ClearValue clearDepth = vk::ClearDepthStencilValue{ 1.0f, 0 };
    vk::RenderingAttachmentInfo depthAttachmentInfo{
        .imageView = *swapchain_->depthImageView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = clearDepth,
    };
    vk::RenderingInfo renderingInfo{
        .renderArea = { .offset = { 0, 0 }, .extent = swapchain_->extent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentInfo,
        .pDepthAttachment = &depthAttachmentInfo,
    };

    cmd.beginRendering(renderingInfo);

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *meshPipeline_->pipeline);
    cmd.bindVertexBuffers(0, *meshBuffer_->vertexBuffer, { vk::DeviceSize{ 0 } });
    cmd.bindIndexBuffer(*meshBuffer_->indexBuffer, 0, vk::IndexType::eUint32);

    vk::Viewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(swapchain_->extent.width),
        .height = static_cast<float>(swapchain_->extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, vk::Rect2D{ .offset = { 0, 0 }, .extent = swapchain_->extent });

    for (auto const& obj: gameObjects_) {
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *meshPipeline_->pipelineLayout, 0,
                *obj.descriptorSets[frameIndex_], {});
        cmd.drawIndexed(static_cast<uint32_t>(meshBuffer_->indices.size()), 1, 0, 0, 0);
    }

    if (scene_.particles) {
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *particlePipeline_->particlePipeline);
        cmd.bindVertexBuffers(0, *particlePipeline_->shaderStorageBuffers[frameIndex_],
                { vk::DeviceSize{ 0 } });
        cmd.draw(scene_.particles->count, 1, 0, 0);
    }

    cmd.endRendering();

    vkutil::transitionImageLayout(cmd, swapchain_->images[imageIndex],
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite, {},
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eBottomOfPipe);

    cmd.end();
}

void Renderer::drawFrame() {
    if (scene_.particles) {
        std::ignore = ctx_->device.waitForFences(*sync_->computeInFlightFences[frameIndex_],
                vk::True, std::numeric_limits<uint64_t>::max());
        ctx_->device.resetFences(*sync_->computeInFlightFences[frameIndex_]);
    }

    updateUniforms();

    if (scene_.particles) {
        cmds_->computeCommandBuffers[frameIndex_].reset();
        recordComputeCommandBuffer(frameIndex_);

        vk::CommandBuffer computeCmdBuf = *cmds_->computeCommandBuffers[frameIndex_];
        vk::SubmitInfo computeSubmitInfo{
            .commandBufferCount = 1,
            .pCommandBuffers = &computeCmdBuf,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &*sync_->computeFinishedSemaphores[frameIndex_],
        };
        ctx_->computeQueue.submit(computeSubmitInfo, *sync_->computeInFlightFences[frameIndex_]);
    }

    std::ignore = ctx_->device.waitForFences(*sync_->inFlightFences[frameIndex_], vk::True,
            std::numeric_limits<uint64_t>::max());

    auto [acquireResult, imageIndex] =
            swapchain_->swapChain.acquireNextImage(std::numeric_limits<uint64_t>::max(),
                    *sync_->presentCompleteSemaphores[frameIndex_], nullptr);

    if (acquireResult == vk::Result::eErrorOutOfDateKHR) {
        recreateSwapchain();
        return;
    }
    if (acquireResult != vk::Result::eSuccess && acquireResult != vk::Result::eSuboptimalKHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    ctx_->device.resetFences(*sync_->inFlightFences[frameIndex_]);

    cmds_->commandBuffers[frameIndex_].reset();
    recordCommandBuffer(imageIndex);

    std::vector<vk::Semaphore> waitSemaphores = { *sync_->presentCompleteSemaphores[frameIndex_] };
    std::vector<vk::PipelineStageFlags> waitStages = {
        vk::PipelineStageFlagBits::eColorAttachmentOutput
    };
    if (scene_.particles) {
        waitSemaphores.push_back(*sync_->computeFinishedSemaphores[frameIndex_]);
        waitStages.push_back(vk::PipelineStageFlagBits::eVertexInput);
    }
    vk::CommandBuffer cmdBuf = *cmds_->commandBuffers[frameIndex_];
    vk::SubmitInfo submitInfo{
        .waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size()),
        .pWaitSemaphores = waitSemaphores.data(),
        .pWaitDstStageMask = waitStages.data(),
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdBuf,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &*sync_->renderFinishedSemaphores[imageIndex],
    };
    ctx_->graphicsQueue.submit(submitInfo, *sync_->inFlightFences[frameIndex_]);

    vk::SwapchainKHR swapChainHandle = *swapchain_->swapChain;
    vk::PresentInfoKHR presentInfo{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*sync_->renderFinishedSemaphores[imageIndex],
        .swapchainCount = 1,
        .pSwapchains = &swapChainHandle,
        .pImageIndices = &imageIndex,
    };
    vk::Result presentResult = ctx_->graphicsQueue.presentKHR(presentInfo);
    if (presentResult == vk::Result::eErrorOutOfDateKHR ||
            presentResult == vk::Result::eSuboptimalKHR || framebufferResized_) {
        framebufferResized_ = false;
        recreateSwapchain();
    } else if (presentResult != vk::Result::eSuccess) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    frameIndex_ = (frameIndex_ + 1) % MAX_FRAMES_IN_FLIGHT;
}
