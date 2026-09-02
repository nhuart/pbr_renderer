#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

#include "core/context.hpp"
#include "core/resource_allocator.hpp"

// ---------------------------------------------------------------------------
// Resource handles — typed indices, no string lookup at runtime
// ---------------------------------------------------------------------------

struct RenderGraphImageHandle {
    uint32_t index = ~0u;
    [[nodiscard]] bool isValid() const { return index != ~0u; }
};

// ---------------------------------------------------------------------------
// Image descriptor — what you declare, not what gets allocated
// ---------------------------------------------------------------------------

struct RenderGraphImageDesc {
    vk::Format format;
    vk::Extent2D extent;
    vk::ImageUsageFlags usage;
    vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
};

// ---------------------------------------------------------------------------
// Internal pass representation
// ---------------------------------------------------------------------------

struct RenderGraphPass {
    std::string name;
    std::vector<RenderGraphImageHandle> colorWrites;
    RenderGraphImageHandle depthWrite{};
    std::vector<RenderGraphImageHandle> reads;
    RenderGraphImageHandle resolveTarget{};
    std::function<void(vk::raii::CommandBuffer const&)> execute;
};

// ---------------------------------------------------------------------------
// Physical resource — allocated during compile()
// ---------------------------------------------------------------------------

struct RenderGraphPhysicalImage {
    vk::Image image{};
    vk::ImageView viewHandle{}; // non-owning; used when imported
    vk::raii::ImageView ownedView{ nullptr };
    vk::raii::Image ownedImage{ nullptr };
    vk::raii::DeviceMemory memory{ nullptr };
    vk::ImageLayout currentLayout = vk::ImageLayout::eUndefined;
    RenderGraphImageDesc desc{};
    bool imported = false;

    [[nodiscard]] vk::ImageView view() const { return imported ? viewHandle : *ownedView; }
};

// ---------------------------------------------------------------------------
// Pre-compiled barrier for a single image transition
// ---------------------------------------------------------------------------

struct RenderGraphBarrier {
    vk::Image image{};
    uint32_t resourceIndex = ~0u; // index into mImages — used to patch on swapchain flip
    vk::ImageLayout oldLayout{};
    vk::ImageLayout newLayout{};
    vk::AccessFlags2 srcAccess{};
    vk::AccessFlags2 dstAccess{};
    vk::PipelineStageFlags2 srcStage{};
    vk::PipelineStageFlags2 dstStage{};
    vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
};

// ---------------------------------------------------------------------------
// Compiled pass — ready to execute
// ---------------------------------------------------------------------------

struct RenderGraphCompiledPass {
    RenderGraphPass const* pass = nullptr;
    std::vector<RenderGraphBarrier> preBarriers;
};

// ---------------------------------------------------------------------------
// RenderGraph
// ---------------------------------------------------------------------------

class RenderGraph;

class RenderGraphPassBuilder {
public:
    RenderGraphPassBuilder(RenderGraph& graph, RenderGraphPass& pass)
            : mGraph(graph),
              mPass(pass) {}

    RenderGraphPassBuilder& writesColor(RenderGraphImageHandle imageHandle) {
        mPass.colorWrites.push_back(imageHandle);
        return *this;
    }
    RenderGraphPassBuilder& writesDepth(RenderGraphImageHandle imageHandle) {
        mPass.depthWrite = imageHandle;
        return *this;
    }
    RenderGraphPassBuilder& reads(RenderGraphImageHandle imageHandle) {
        mPass.reads.push_back(imageHandle);
        return *this;
    }
    RenderGraphPassBuilder& resolvesTo(RenderGraphImageHandle imageHandle) {
        mPass.resolveTarget = imageHandle;
        return *this;
    }
    RenderGraphPassBuilder& execute(
            std::function<void(vk::raii::CommandBuffer const&)> executeFunction) {
        mPass.execute = std::move(executeFunction);
        return *this;
    }

private:
    RenderGraph& mGraph;
    RenderGraphPass& mPass;
};

class RenderGraph {
public:
    [[nodiscard]] RenderGraphImageHandle declareImage(std::string name, RenderGraphImageDesc desc);

    [[nodiscard]] RenderGraphImageHandle importImage(std::string name, vk::Image image,
            vk::ImageView view, RenderGraphImageDesc desc,
            vk::ImageLayout initialLayout = vk::ImageLayout::eUndefined);

    [[nodiscard]] RenderGraphPassBuilder addPass(std::string name);

    // Update the backing of an imported image each frame (e.g. current swapchain image).
    // Takes a non-owning view — the caller owns the view's lifetime.
    void updateImportedImage(RenderGraphImageHandle handle, vk::Image image, vk::ImageView view);

    void compile(VulkanContext const& ctx);
    void execute(vk::raii::CommandBuffer const& commandBuffer);

    [[nodiscard]] vk::ImageView imageView(RenderGraphImageHandle handle) const;
    [[nodiscard]] vk::Extent2D extent(RenderGraphImageHandle handle) const;

private:
    std::vector<RenderGraphPhysicalImage> mImages;
    std::vector<std::string> mImageNames;
    std::vector<RenderGraphPass> mPasses;
    std::vector<RenderGraphCompiledPass> mCompiledPasses;

    void allocateImages(VulkanContext const& ctx);
    void buildBarriers();

    static void insertBarriers(vk::raii::CommandBuffer const& commandBuffer,
            std::vector<RenderGraphBarrier> const& barriers);

    static std::pair<vk::AccessFlags2, vk::PipelineStageFlags2> accessForLayout(
            vk::ImageLayout layout);

    [[nodiscard]] vk::Extent2D resolveExtent(RenderGraphPass const& pass) const;
};
