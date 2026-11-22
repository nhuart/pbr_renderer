#pragma once

#include "renderer/model_asset.hpp"
#include <cstddef>
#include <vulkan/vulkan_raii.hpp>

struct VulkanContext;
struct TextureAtlas;

// Static material inputs shared by color, depth/normal, and shadow passes (set 1).
struct SurfaceUniforms {
    alignas(16) glm::vec4 specularGlossiness{ 1.0f };
    float alphaCutoff = 0.5f;
    float alphaMode = static_cast<float>(AlphaMode::Opaque);
    float workflow = static_cast<float>(MaterialWorkflow::MetallicRoughness);
    float opacityFactor = 1.0f;
};

static_assert(offsetof(SurfaceUniforms, alphaCutoff) == 16);
static_assert(sizeof(SurfaceUniforms) == 32);

struct SurfaceTextures {
    TextureAtlas const& color;
    TextureAtlas const& specularGlossiness;
};

struct MaterialSurface {
    vk::raii::DeviceMemory memory{ nullptr };
    vk::raii::Buffer buffer{ nullptr };
    vk::raii::DescriptorSets descriptorSets{ nullptr };
};

struct MaterialSurfacePool {
    vk::raii::DescriptorSetLayout layout{ nullptr };
    vk::raii::DescriptorPool pool{ nullptr };

    MaterialSurfacePool(VulkanContext const& ctx, uint32_t count);
    MaterialSurface create(VulkanContext const& ctx, SurfaceUniforms const& uniforms,
            SurfaceTextures const& textures) const;
};
