#include "renderer/renderer.hpp"

#include "core/resource_allocator.hpp"

#include <algorithm>

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
                    }, vk::ImageLayout::eUndefined, true);

    bool multisampled = mCtx->msaaSamples != vk::SampleCountFlagBits::e1;
    RenderGraphImageHandle colorImage{};
    if (multisampled) {
        colorImage = mRenderGraph.importImage("color", *mSwapchain->colorImage,
                *mSwapchain->colorImageView, RenderGraphImage{
                    .format = Swapchain::HDR_COLOR_FORMAT,
                    .extent = swapchainExtent,
                    .usage = vk::ImageUsageFlagBits::eColorAttachment,
                    .aspect = vk::ImageAspectFlagBits::eColor,
                    .samples = mCtx->msaaSamples,
                });
    }

    auto hdrImage = importHdrColorImage();

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
                                .writesColor(multisampled ? colorImage : hdrImage)
                                .writesDepth(depthImage);
    if (multisampled) {
        forwardPass.resolvesTo(hdrImage);
    }
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

    addTonemapPass(hdrImage);

    mRenderGraph.compile(*mCtx);
}

RenderGraphImageHandle Renderer::importHdrColorImage() {
    return mRenderGraph.importImage("hdrColor", *mSwapchain->hdrImage,
            *mSwapchain->hdrImageView, RenderGraphImage{
                .format = Swapchain::HDR_COLOR_FORMAT,
                .extent = mSwapchain->extent,
                .usage = vk::ImageUsageFlagBits::eColorAttachment |
                         vk::ImageUsageFlagBits::eSampled,
                .aspect = vk::ImageAspectFlagBits::eColor,
            });
}

void Renderer::addTonemapPass(RenderGraphImageHandle hdrColor) {
    mRenderGraph.addPass("TonemapPass")
            .writesColor(mSwapchainImageHandle)
            .reads(hdrColor)
            .execute([this](vk::raii::CommandBuffer const& cmd) {
                setViewportAndScissor(cmd, mSwapchain->extent);
                mTonemapPipeline->draw(cmd);
            });
}
