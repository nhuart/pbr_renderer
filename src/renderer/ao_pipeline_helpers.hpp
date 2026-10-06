#pragma once

#include "core/resource_allocator.hpp"

#include <vector>

namespace ao_detail {

struct FullscreenPipelineState {
    std::vector<vk::DynamicState> dynStates;
    vk::PipelineDynamicStateCreateInfo dynInfo;
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
    vk::PipelineInputAssemblyStateCreateInfo iaInfo;
    vk::PipelineViewportStateCreateInfo vpInfo;
    vk::PipelineRasterizationStateCreateInfo rsInfo;
    vk::PipelineMultisampleStateCreateInfo msInfo;
    vk::PipelineColorBlendAttachmentState cbAttach;
    vk::PipelineColorBlendStateCreateInfo cbInfo;
};

inline FullscreenPipelineState makeFullscreenPipelineState() {
    FullscreenPipelineState state;
    state.dynStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    state.dynInfo = vk::PipelineDynamicStateCreateInfo{
        .dynamicStateCount = static_cast<uint32_t>(state.dynStates.size()),
        .pDynamicStates = state.dynStates.data(),
    };
    state.vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{};
    state.iaInfo = vk::PipelineInputAssemblyStateCreateInfo{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };
    state.vpInfo = vk::PipelineViewportStateCreateInfo{ .viewportCount = 1, .scissorCount = 1 };
    state.rsInfo = vk::PipelineRasterizationStateCreateInfo{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .lineWidth = 1.0f,
    };
    state.msInfo = vk::PipelineMultisampleStateCreateInfo{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
    };
    state.cbAttach = vk::PipelineColorBlendAttachmentState{
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    state.cbInfo = vk::PipelineColorBlendStateCreateInfo{
        .attachmentCount = 1,
        .pAttachments = &state.cbAttach,
    };
    return state;
}

inline std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> makeUboBuffer(VulkanContext const& ctx,
        vk::DeviceSize size) {
    return vkutil::createBuffer(ctx, size, vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
}

} // namespace ao_detail
