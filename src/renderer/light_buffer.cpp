#include "renderer/light_buffer.hpp"
#include "core/resource_allocator.hpp"

LightBuffer::LightBuffer(VulkanContext const& ctx, LightUBO const& data) {
    auto [buf, mem] = vkutil::createBuffer(ctx, sizeof(LightUBO),
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    mapped = mem.mapMemory(0, sizeof(LightUBO));
    memcpy(mapped, &data, sizeof(LightUBO));
    buffer = std::move(buf);
    memory = std::move(mem);
}
