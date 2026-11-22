#pragma once

#include "renderer/embedded_image.hpp"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan.hpp>
#else
import vulkan_hpp;
#endif

struct IndexRange {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
};

struct TextureSampler {
    vk::Filter minFilter = vk::Filter::eLinear;
    vk::Filter magFilter = vk::Filter::eLinear;
    vk::SamplerMipmapMode mipmapMode = vk::SamplerMipmapMode::eLinear;
    vk::SamplerAddressMode wrapU = vk::SamplerAddressMode::eRepeat;
    vk::SamplerAddressMode wrapV = vk::SamplerAddressMode::eRepeat;
    bool useMipmaps = true;
};

struct TextureDefinition {
    int image = -1;
    TextureSampler sampler;
};

struct TextureReference {
    int texture = -1;
    int texCoord = 0;

    bool hasTexture() const { return texture >= 0; }
};

// Numeric values are part of the SurfaceUniforms / material_surface.glsl GPU interface.
enum class MaterialWorkflow { MetallicRoughness = 0, SpecularGlossiness = 1 };
enum class AlphaMode { Opaque = 0, Mask = 1, Blend = 2 };

struct MaterialDefinition {
    glm::vec4 baseColor{ 1.0f };
    float metallic = 1.0f;
    float roughness = 1.0f;
    float normalScale = 1.0f;
    bool doubleSided = false;
    TextureReference baseColorTexture;
    TextureReference normalTexture;
    TextureReference specularGlossinessTexture;
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
};

struct MeshPrimitive {
    IndexRange range;
    uint32_t materialIndex = 0;
    glm::vec3 center{};
};

struct ModelAsset {
    IndexRange range;
    glm::vec3 boundsMin{};
    glm::vec3 boundsMax{};
    std::vector<MeshPrimitive> primitives;
    std::vector<MaterialDefinition> materials;
    std::vector<TextureDefinition> textures;
    std::vector<EmbeddedImage> images;
};
