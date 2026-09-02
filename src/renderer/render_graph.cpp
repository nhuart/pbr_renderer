#include "renderer/render_graph.hpp"

RenderGraphImageHandle RenderGraph::declareImage(std::string name, RenderGraphImageDesc desc) {
    RenderGraphImageHandle handle{ static_cast<uint32_t>(mImages.size()) };
    mImages.push_back({ .desc = desc });
    mImageNames.push_back(std::move(name));
    return handle;
}

RenderGraphImageHandle RenderGraph::importImage(std::string name, vk::Image image,
        vk::ImageView view, RenderGraphImageDesc desc, vk::ImageLayout initialLayout) {
    RenderGraphImageHandle handle{ static_cast<uint32_t>(mImages.size()) };
    RenderGraphPhysicalImage physicalImage;
    physicalImage.image = image;
    physicalImage.viewHandle = view;
    physicalImage.currentLayout = initialLayout;
    physicalImage.desc = desc;
    physicalImage.imported = true;
    mImages.push_back(std::move(physicalImage));
    mImageNames.push_back(std::move(name));
    return handle;
}

void RenderGraph::updateImportedImage(RenderGraphImageHandle handle, vk::Image image,
        vk::ImageView view) {
    auto& physicalImage = mImages[handle.index];
    physicalImage.image = image;
    physicalImage.viewHandle = view;
    for (auto& compiledPass: mCompiledPasses)
        for (auto& barrier: compiledPass.preBarriers)
            if (barrier.resourceIndex == handle.index) barrier.image = image;
}

RenderGraphPassBuilder RenderGraph::addPass(std::string name) {
    mPasses.push_back(RenderGraphPass{ .name = std::move(name) });
    return RenderGraphPassBuilder{ *this, mPasses.back() };
}

void RenderGraph::compile(VulkanContext const& ctx) {
    allocateImages(ctx);
    buildBarriers();
}

void RenderGraph::execute(vk::raii::CommandBuffer const& commandBuffer) {
    for (auto const& compiledPass: mCompiledPasses) {
        insertBarriers(commandBuffer, compiledPass.preBarriers);

        auto const& pass = *compiledPass.pass;
        if (!pass.execute) continue;

        std::vector<vk::RenderingAttachmentInfo> colorAttachments;
        for (auto imageHandle: pass.colorWrites) {
            auto& physicalImage = mImages[imageHandle.index];
            vk::RenderingAttachmentInfo colorAttachmentInfo{
                .imageView = physicalImage.view(),
                .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp = vk::AttachmentStoreOp::eDontCare,
                .clearValue = vk::ClearColorValue{ 1.0f, 1.0f, 1.0f, 1.0f },
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
            depthAttachmentInfo = {
                .imageView = physicalImage.view(),
                .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp = vk::AttachmentStoreOp::eDontCare,
                .clearValue = vk::ClearDepthStencilValue{ 1.0f, 0 },
            };
        }

        vk::Extent2D extent = resolveExtent(pass);
        commandBuffer.beginRendering(vk::RenderingInfo{
            .renderArea = { .offset = { 0, 0 }, .extent = extent },
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

void RenderGraph::allocateImages(VulkanContext const& ctx) {
    for (auto& physicalImage: mImages) {
        if (physicalImage.imported) continue;
        auto const& imageDesc = physicalImage.desc;
        auto [image, memory] =
                vkutil::createImage(ctx, imageDesc.extent.width, imageDesc.extent.height, 1,
                        imageDesc.samples, imageDesc.format, vk::ImageTiling::eOptimal,
                        imageDesc.usage, vk::MemoryPropertyFlagBits::eDeviceLocal);
        physicalImage.ownedImage = std::move(image);
        physicalImage.memory = std::move(memory);
        physicalImage.image = *physicalImage.ownedImage;
        physicalImage.ownedView = vkutil::createImageView(ctx, physicalImage.image,
                imageDesc.format, imageDesc.aspect);
    }
}

void RenderGraph::buildBarriers() {
    mCompiledPasses.clear();
    std::vector<vk::ImageLayout> layouts(mImages.size(), vk::ImageLayout::eUndefined);

    for (auto const& pass: mPasses) {
        RenderGraphCompiledPass compiledPass{ .pass = &pass };

        auto addBarrier = [&](RenderGraphImageHandle imageHandle, vk::ImageLayout newLayout,
                                  vk::AccessFlags2 dstAccess, vk::PipelineStageFlags2 dstStage) {
            if (!imageHandle.isValid()) return;
            auto& physicalImage = mImages[imageHandle.index];
            vk::ImageLayout oldLayout = layouts[imageHandle.index];
            if (oldLayout == newLayout) return;
            auto [srcAccess, srcStage] = accessForLayout(oldLayout);
            compiledPass.preBarriers.push_back({
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
            if (pass.resolveTarget.isValid())
                addBarrier(pass.resolveTarget, vk::ImageLayout::eColorAttachmentOptimal,
                        vk::AccessFlagBits2::eColorAttachmentWrite,
                        vk::PipelineStageFlagBits2::eColorAttachmentOutput);
        }
        if (pass.depthWrite.isValid())
            addBarrier(pass.depthWrite, vk::ImageLayout::eDepthAttachmentOptimal,
                    vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                    vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                            vk::PipelineStageFlagBits2::eLateFragmentTests);
        for (auto imageHandle: pass.reads)
            addBarrier(imageHandle, vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::AccessFlagBits2::eShaderRead, vk::PipelineStageFlagBits2::eFragmentShader);

        mCompiledPasses.push_back(std::move(compiledPass));
    }

    // Final transition: swapchain resolve target → present
    for (auto& physicalImage: mImages) {
        if (!physicalImage.imported) continue;
        uint32_t resourceIndex = static_cast<uint32_t>(&physicalImage - mImages.data());
        if (layouts[resourceIndex] == vk::ImageLayout::eColorAttachmentOptimal) {
            mCompiledPasses.back().preBarriers.push_back({
                .image = physicalImage.image,
                .resourceIndex = resourceIndex,
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
    if (barriers.empty()) return;
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
    if (!pass.colorWrites.empty()) return mImages[pass.colorWrites[0].index].desc.extent;
    if (pass.depthWrite.isValid()) return mImages[pass.depthWrite.index].desc.extent;
    return {};
}
