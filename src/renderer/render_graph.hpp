#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

#include "core/context.hpp"

struct RenderGraphImageHandle {
    uint32_t index = ~0u;
    [[nodiscard]] bool isValid() const { return index != ~0u; }
};

struct RenderGraphImage {
    vk::Format format;
    vk::Extent2D extent;
    vk::ImageUsageFlags usage;
    vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
    uint32_t mipLevel = 0;
};

struct RenderGraphBarrier {
    vk::Image image{};
    uint32_t resourceIndex = ~0u;
    vk::ImageLayout oldLayout{};
    vk::ImageLayout newLayout{};
    vk::AccessFlags2 srcAccess{};
    vk::AccessFlags2 dstAccess{};
    vk::PipelineStageFlags2 srcStage{};
    vk::PipelineStageFlags2 dstStage{};
    vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
    uint32_t mipLevel = 0;
};

struct RenderGraphPass {
    std::string name;
    std::vector<RenderGraphImageHandle> colorWrites;
    RenderGraphImageHandle depthWrite{};
    std::vector<RenderGraphImageHandle> reads;
    RenderGraphImageHandle resolveTarget{};
    std::function<void(vk::raii::CommandBuffer const&)> execute;
    std::vector<RenderGraphBarrier> preBarriers;
    std::vector<RenderGraphBarrier> postBarriers;
};

struct RenderGraphPhysicalImage {
    vk::Image image{};
    vk::ImageView viewHandle{};
    vk::ImageLayout currentLayout = vk::ImageLayout::eUndefined;
    RenderGraphImage desc{};
    bool presentable = false;

    [[nodiscard]] vk::ImageView view() const { return viewHandle; }
};

class RenderGraph {
public:
    [[nodiscard]] RenderGraphImageHandle importImage(std::string name, vk::Image image,
            vk::ImageView view, RenderGraphImage desc,
            vk::ImageLayout initialLayout = vk::ImageLayout::eUndefined, bool presentable = false);

    void updateImportedImage(RenderGraphImageHandle handle, vk::Image image, vk::ImageView view);

    RenderGraph& addPass(std::string name);
    RenderGraph& writesColor(RenderGraphImageHandle handle);
    RenderGraph& writesDepth(RenderGraphImageHandle handle);
    RenderGraph& reads(RenderGraphImageHandle handle);
    RenderGraph& resolvesTo(RenderGraphImageHandle handle);
    RenderGraph& execute(std::function<void(vk::raii::CommandBuffer const&)> fn);

    void compile(VulkanContext const& ctx);
    void execute(vk::raii::CommandBuffer const& commandBuffer);

    [[nodiscard]] vk::ImageView imageView(RenderGraphImageHandle handle) const;
    [[nodiscard]] vk::Extent2D extent(RenderGraphImageHandle handle) const;

private:
    std::vector<RenderGraphPhysicalImage> mImages;
    std::vector<RenderGraphPass> mPasses;
    // image indices read by any pass after they are written
    // use this set to determine which images the gpu needs to keep in memory between passes
    std::unordered_set<uint32_t> mSubsequentlyRead;

    void buildBarriers();

    static void insertBarriers(vk::raii::CommandBuffer const& commandBuffer,
            std::vector<RenderGraphBarrier> const& barriers);

    static std::pair<vk::AccessFlags2, vk::PipelineStageFlags2> accessForLayout(
            vk::ImageLayout layout);

    [[nodiscard]] vk::Extent2D resolveExtent(RenderGraphPass const& pass) const;
};
