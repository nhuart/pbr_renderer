#include "renderer/render_graph.hpp"

RenderGraphImageHandle RenderGraph::importImage(std::string /*name*/, vk::Image image,
        vk::ImageView view, RenderGraphImage desc, vk::ImageLayout initialLayout) {
    RenderGraphImageHandle handle{ static_cast<uint32_t>(mImages.size()) };
    mImages.push_back(
            { .image = image, .viewHandle = view, .currentLayout = initialLayout, .desc = desc });
    return handle;
}

void RenderGraph::updateImportedImage(RenderGraphImageHandle handle, vk::Image image,
        vk::ImageView view) {
    auto& physicalImage = mImages[handle.index];
    physicalImage.image = image;
    physicalImage.viewHandle = view;
    for (auto& pass: mPasses) {
        for (auto& barrier: pass.preBarriers) {
            if (barrier.resourceIndex == handle.index) {
                barrier.image = image;
            }
        }
    }
}

RenderGraph& RenderGraph::addPass(std::string name) {
    mPasses.push_back(RenderGraphPass{ .name = std::move(name) });
    return *this;
}

RenderGraph& RenderGraph::writesColor(RenderGraphImageHandle handle) {
    mPasses.back().colorWrites.push_back(handle);
    return *this;
}

RenderGraph& RenderGraph::writesDepth(RenderGraphImageHandle handle) {
    mPasses.back().depthWrite = handle;
    return *this;
}

RenderGraph& RenderGraph::reads(RenderGraphImageHandle handle) {
    mPasses.back().reads.push_back(handle);
    return *this;
}

RenderGraph& RenderGraph::resolvesTo(RenderGraphImageHandle handle) {
    mPasses.back().resolveTarget = handle;
    return *this;
}

RenderGraph& RenderGraph::execute(std::function<void(vk::raii::CommandBuffer const&)> fn) {
    mPasses.back().execute = std::move(fn);
    return *this;
}

void RenderGraph::compile(VulkanContext const& /*ctx*/) { buildBarriers(); }

void RenderGraph::execute(vk::raii::CommandBuffer const& commandBuffer) {
    for (auto const& pass: mPasses) {
        insertBarriers(commandBuffer, pass.preBarriers);

        if (!pass.execute) {
            continue;
        }

        std::vector<vk::RenderingAttachmentInfo> colorAttachments;
        for (auto imageHandle: pass.colorWrites) {
            auto& physicalImage = mImages[imageHandle.index];
            bool isRead = mSubsequentlyRead.count(imageHandle.index) > 0;
            vk::RenderingAttachmentInfo colorAttachmentInfo{
                .imageView = physicalImage.view(),
                .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp =
                        isRead ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare,
                .clearValue = vk::ClearColorValue{ 0.0f, 0.0f, 0.0f, 0.0f },
            };
            if (pass.resolveTarget.isValid()) {
                auto& resolveImage = mImages[pass.resolveTarget.index];
                colorAttachmentInfo.resolveMode = vk::ResolveModeFlagBits::eAverage;
                colorAttachmentInfo.resolveImageView = resolveImage.view();
                colorAttachmentInfo.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            }
            colorAttachments.push_back(colorAttachmentInfo);
        }

        vk::RenderingAttachmentInfo depthAttachmentInfo{};
        bool hasDepth = pass.depthWrite.isValid();
        if (hasDepth) {
            auto& physicalImage = mImages[pass.depthWrite.index];
            bool isRead = mSubsequentlyRead.count(pass.depthWrite.index) > 0;
            depthAttachmentInfo = {
                .imageView = physicalImage.view(),
                .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp =
                        isRead ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare,
                .clearValue = vk::ClearDepthStencilValue{ 1.0f, 0 },
            };
        }

        vk::Extent2D ext = resolveExtent(pass);
        commandBuffer.beginRendering(vk::RenderingInfo{
            .renderArea = { .offset = { 0, 0 }, .extent = ext },
            .layerCount = 1,
            .colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size()),
            .pColorAttachments = colorAttachments.data(),
            .pDepthAttachment = hasDepth ? &depthAttachmentInfo : nullptr,
        });
        pass.execute(commandBuffer);
        commandBuffer.endRendering();
    }
}

vk::ImageView RenderGraph::imageView(RenderGraphImageHandle handle) const {
    return mImages[handle.index].view();
}

vk::Extent2D RenderGraph::extent(RenderGraphImageHandle handle) const {
    return mImages[handle.index].desc.extent;
}

void RenderGraph::buildBarriers() {
    mSubsequentlyRead.clear();
    for (auto const& pass: mPasses) {
        for (auto imageHandle: pass.reads) {
            mSubsequentlyRead.insert(imageHandle.index);
        }
    }

    std::vector<vk::ImageLayout> layouts(mImages.size(), vk::ImageLayout::eUndefined);
    for (size_t i = 0; i < mImages.size(); ++i) {
        layouts[i] = mImages[i].currentLayout;
    }

    for (auto& pass: mPasses) {
        pass.preBarriers.clear();

        auto addBarrier = [&](RenderGraphImageHandle imageHandle, vk::ImageLayout newLayout,
                                  vk::AccessFlags2 dstAccess, vk::PipelineStageFlags2 dstStage) {
            if (!imageHandle.isValid()) {
                return;
            }
            auto& physicalImage = mImages[imageHandle.index];
            vk::ImageLayout oldLayout = layouts[imageHandle.index];
            if (oldLayout == newLayout) {
                return;
            }
            auto [srcAccess, srcStage] = accessForLayout(oldLayout);
            pass.preBarriers.push_back({
                .image = physicalImage.image,
                .resourceIndex = imageHandle.index,
                .oldLayout = oldLayout,
                .newLayout = newLayout,
                .srcAccess = srcAccess,
                .dstAccess = dstAccess,
                .srcStage = srcStage,
                .dstStage = dstStage,
                .aspect = physicalImage.desc.aspect,
            });
            layouts[imageHandle.index] = newLayout;
        };

        for (auto imageHandle: pass.colorWrites) {
            addBarrier(imageHandle, vk::ImageLayout::eColorAttachmentOptimal,
                    vk::AccessFlagBits2::eColorAttachmentWrite,
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput);
            if (pass.resolveTarget.isValid()) {
                addBarrier(pass.resolveTarget, vk::ImageLayout::eColorAttachmentOptimal,
                        vk::AccessFlagBits2::eColorAttachmentWrite,
                        vk::PipelineStageFlagBits2::eColorAttachmentOutput);
            }
        }
        if (pass.depthWrite.isValid()) {
            addBarrier(pass.depthWrite, vk::ImageLayout::eDepthAttachmentOptimal,
                    vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                    vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                            vk::PipelineStageFlagBits2::eLateFragmentTests);
        }
        for (auto imageHandle: pass.reads) {
            addBarrier(imageHandle, vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::AccessFlagBits2::eShaderRead, vk::PipelineStageFlagBits2::eFragmentShader);
        }
    }

    // Transition imported images that ended as color attachments to present layout.
    for (size_t i = 0; i < mImages.size(); ++i) {
        if (layouts[i] != vk::ImageLayout::eColorAttachmentOptimal) {
            continue;
        }
        auto& physicalImage = mImages[i];
        if (physicalImage.desc.usage & vk::ImageUsageFlagBits::eColorAttachment &&
                physicalImage.desc.samples == vk::SampleCountFlagBits::e1) {
            mPasses.back().preBarriers.push_back({
                .image = physicalImage.image,
                .resourceIndex = static_cast<uint32_t>(i),
                .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .newLayout = vk::ImageLayout::ePresentSrcKHR,
                .srcAccess = vk::AccessFlagBits2::eColorAttachmentWrite,
                .dstAccess = {},
                .srcStage = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                .dstStage = vk::PipelineStageFlagBits2::eBottomOfPipe,
                .aspect = vk::ImageAspectFlagBits::eColor,
            });
        }
    }
}

void RenderGraph::insertBarriers(vk::raii::CommandBuffer const& commandBuffer,
        std::vector<RenderGraphBarrier> const& barriers) {
    if (barriers.empty()) {
        return;
    }
    std::vector<vk::ImageMemoryBarrier2> vkBarriers;
    vkBarriers.reserve(barriers.size());
    for (auto const& barrier: barriers) {
        vkBarriers.push_back(vk::ImageMemoryBarrier2{
            .srcStageMask        = barrier.srcStage,
            .srcAccessMask       = barrier.srcAccess,
            .dstStageMask        = barrier.dstStage,
            .dstAccessMask       = barrier.dstAccess,
            .oldLayout           = barrier.oldLayout,
            .newLayout           = barrier.newLayout,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image               = barrier.image,
            .subresourceRange    = {
                .aspectMask     = barrier.aspect,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        });
    }
    commandBuffer.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = static_cast<uint32_t>(vkBarriers.size()),
        .pImageMemoryBarriers = vkBarriers.data(),
    });
}

std::pair<vk::AccessFlags2, vk::PipelineStageFlags2> RenderGraph::accessForLayout(
        vk::ImageLayout layout) {
    using Access = vk::AccessFlagBits2;
    using Stage = vk::PipelineStageFlagBits2;
    switch (layout) {
        case vk::ImageLayout::eColorAttachmentOptimal:
            return { Access::eColorAttachmentWrite, Stage::eColorAttachmentOutput };
        case vk::ImageLayout::eDepthAttachmentOptimal:
            return { Access::eDepthStencilAttachmentWrite,
                Stage::eEarlyFragmentTests | Stage::eLateFragmentTests };
        case vk::ImageLayout::eShaderReadOnlyOptimal:
            return { Access::eShaderRead, Stage::eFragmentShader };
        case vk::ImageLayout::ePresentSrcKHR:
            return { {}, Stage::eBottomOfPipe };
        default:
            return { {}, Stage::eTopOfPipe };
    }
}

vk::Extent2D RenderGraph::resolveExtent(RenderGraphPass const& pass) const {
    if (!pass.colorWrites.empty()) {
        return mImages[pass.colorWrites[0].index].desc.extent;
    }
    if (pass.depthWrite.isValid()) {
        return mImages[pass.depthWrite.index].desc.extent;
    }
    return {};
}
