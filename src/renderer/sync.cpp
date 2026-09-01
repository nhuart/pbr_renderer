#include "core/application.hpp"

#include <limits>
#include <stdexcept>

void Renderer::createSyncObjects() {
  for (size_t i = 0; i < swapChainImages.size(); i++) {
    renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo{});
  }
  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    presentCompleteSemaphores.emplace_back(device, vk::SemaphoreCreateInfo{});
    inFlightFences.emplace_back(device, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
    computeFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo{});
    computeInFlightFences.emplace_back(device, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
  }
}

void Renderer::drawFrame() {
  // --- Compute pass ---
  std::ignore =
      device.waitForFences(*computeInFlightFences[frameIndex], vk::True, std::numeric_limits<uint64_t>::max());
  device.resetFences(*computeInFlightFences[frameIndex]);

  updateUniformBuffer();

  computeCommandBuffers[frameIndex].reset();
  recordComputeCommandBuffer(frameIndex);

  vk::CommandBuffer computeCmdBuf = *computeCommandBuffers[frameIndex];
  vk::SubmitInfo computeSubmitInfo{
      .commandBufferCount  = 1,
      .pCommandBuffers     = &computeCmdBuf,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores   = &*computeFinishedSemaphores[frameIndex],
  };
  computeQueue.submit(computeSubmitInfo, *computeInFlightFences[frameIndex]);

  // --- Graphics pass ---
  std::ignore = device.waitForFences(*inFlightFences[frameIndex], vk::True, std::numeric_limits<uint64_t>::max());

  auto [acquireResult, imageIndex] = swapChain.acquireNextImage(
      std::numeric_limits<uint64_t>::max(), *presentCompleteSemaphores[frameIndex], nullptr);

  if (acquireResult == vk::Result::eErrorOutOfDateKHR) {
    recreateSwapChain();
    return;
  }
  if (acquireResult != vk::Result::eSuccess && acquireResult != vk::Result::eSuboptimalKHR) {
    throw std::runtime_error("failed to acquire swap chain image!");
  }

  // Reset fence only after confirming we will submit — avoids unsignalled fence deadlock.
  device.resetFences(*inFlightFences[frameIndex]);

  commandBuffers[frameIndex].reset();
  recordCommandBuffer(imageIndex);

  std::array waitSemaphores = {*presentCompleteSemaphores[frameIndex], *computeFinishedSemaphores[frameIndex]};
  std::array<vk::PipelineStageFlags, 2> waitStages = {vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                                      vk::PipelineStageFlagBits::eVertexInput};
  vk::CommandBuffer cmdBuf = *commandBuffers[frameIndex];
  vk::SubmitInfo submitInfo{
      .waitSemaphoreCount  = static_cast<uint32_t>(waitSemaphores.size()),
      .pWaitSemaphores     = waitSemaphores.data(),
      .pWaitDstStageMask   = waitStages.data(),
      .commandBufferCount  = 1,
      .pCommandBuffers     = &cmdBuf,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores   = &*renderFinishedSemaphores[imageIndex],
  };
  graphicsQueue.submit(submitInfo, *inFlightFences[frameIndex]);

  vk::SwapchainKHR swapChainHandle = *swapChain;
  vk::PresentInfoKHR presentInfo{
      .waitSemaphoreCount = 1,
      .pWaitSemaphores    = &*renderFinishedSemaphores[imageIndex],
      .swapchainCount     = 1,
      .pSwapchains        = &swapChainHandle,
      .pImageIndices      = &imageIndex,
  };
  vk::Result presentResult = graphicsQueue.presentKHR(presentInfo);
  if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR ||
      framebufferResized) {
    framebufferResized = false;
    recreateSwapChain();
  } else if (presentResult != vk::Result::eSuccess) {
    throw std::runtime_error("failed to present swap chain image!");
  }

  frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}
