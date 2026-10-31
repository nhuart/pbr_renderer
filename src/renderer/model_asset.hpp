#pragma once

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
};

struct EmbeddedImage {
    std::vector<uint8_t> pixels; // RGBA8, vertically flipped to match imported UVs.
    uint32_t width = 0;
    uint32_t height = 0;
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

struct MaterialDefinition {
    glm::vec4 baseColor{ 1.0f };
    float metallic = 1.0f;
    float roughness = 1.0f;
    float normalScale = 1.0f;
    bool doubleSided = false;
    TextureReference baseColorTexture;
    TextureReference normalTexture;
};

struct MeshPrimitive {
    IndexRange range;
    uint32_t materialIndex = 0;
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
