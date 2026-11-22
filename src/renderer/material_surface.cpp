#include "renderer/material_surface.hpp"
#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "renderer/texture_atlas.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace {

vk::raii::DescriptorSetLayout createSurfaceLayout(VulkanContext const& ctx) {
    std::array bindings{
        vk::DescriptorSetLayoutBinding{ 0, vk::DescriptorType::eUniformBuffer, 1,
            vk::ShaderStageFlagBits::eFragment },
        vk::DescriptorSetLayoutBinding{ 1, vk::DescriptorType::eCombinedImageSampler, 1,
            vk::ShaderStageFlagBits::eFragment },
        vk::DescriptorSetLayoutBinding{ 2, vk::DescriptorType::eCombinedImageSampler, 1,
            vk::ShaderStageFlagBits::eFragment },
    };
    return vk::raii::DescriptorSetLayout(ctx.device,
            vk::DescriptorSetLayoutCreateInfo{ .bindingCount = bindings.size(),
                .pBindings = bindings.data() });
}

vk::raii::DescriptorPool createSurfacePool(VulkanContext const& ctx, uint32_t count) {
    std::array sizes{ vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, count },
        vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, count * 2 } };
    return vk::raii::DescriptorPool(ctx.device,
            vk::DescriptorPoolCreateInfo{
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = count,
                .poolSizeCount = sizes.size(),
                .pPoolSizes = sizes.data() });
}

void uploadUniforms(VulkanContext const& ctx, MaterialSurface& surface,
        SurfaceUniforms const& uniforms) {
    auto [buffer, memory] = vkutil::createBuffer(ctx, sizeof(uniforms),
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    auto* mapped = memory.mapMemory(0, sizeof(uniforms));
    std::memcpy(mapped, &uniforms, sizeof(uniforms));
    memory.unmapMemory();
    surface.buffer = std::move(buffer);
    surface.memory = std::move(memory);
}

vk::WriteDescriptorSet uniformDescriptor(vk::DescriptorSet set,
        vk::DescriptorBufferInfo const& buffer) {
    return { .dstSet = set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .pBufferInfo = &buffer };
}

vk::WriteDescriptorSet textureDescriptor(vk::DescriptorSet set, uint32_t binding,
        vk::DescriptorImageInfo const& image) {
    return { .dstSet = set,
        .dstBinding = binding,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .pImageInfo = &image };
}

vk::DescriptorImageInfo imageDescriptor(TextureAtlas const& texture) {
    return { *texture.sampler, *texture.imageView, vk::ImageLayout::eShaderReadOnlyOptimal };
}

void writeDescriptors(VulkanContext const& ctx, MaterialSurface const& surface,
        SurfaceTextures const& textures) {
    vk::DescriptorBufferInfo uniformBuffer{ *surface.buffer, 0, sizeof(SurfaceUniforms) };
    auto color = imageDescriptor(textures.color);
    auto specularGlossiness = imageDescriptor(textures.specularGlossiness);
    auto descriptorSet = *surface.descriptorSets[0];
    std::array writes{ uniformDescriptor(descriptorSet, uniformBuffer),
        textureDescriptor(descriptorSet, 1, color),
        textureDescriptor(descriptorSet, 2, specularGlossiness) };
    ctx.device.updateDescriptorSets(writes, {});
}

} // namespace

MaterialSurfacePool::MaterialSurfacePool(VulkanContext const& ctx, uint32_t count)
        : layout(createSurfaceLayout(ctx)),
          pool(createSurfacePool(ctx, std::max(count, 1u))) {}

MaterialSurface MaterialSurfacePool::create(VulkanContext const& ctx,
        SurfaceUniforms const& uniforms, SurfaceTextures const& textures) const {
    MaterialSurface surface;
    uploadUniforms(ctx, surface, uniforms);
    vk::DescriptorSetLayout layoutHandle = *layout;
    surface.descriptorSets = vk::raii::DescriptorSets(ctx.device,
            vk::DescriptorSetAllocateInfo{ .descriptorPool = *pool,
                .descriptorSetCount = 1,
                .pSetLayouts = &layoutHandle });
    writeDescriptors(ctx, surface, textures);
    return surface;
}
