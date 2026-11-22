#include "renderer/gltf_material.hpp"

#include <tiny_gltf.h>

MaterialDefinition gltf::readMaterial(tinygltf::Material const& material) {
    auto const& pbr = material.pbrMetallicRoughness;
    return { .baseColor = glm::vec4(pbr.baseColorFactor[0], pbr.baseColorFactor[1],
                     pbr.baseColorFactor[2], pbr.baseColorFactor[3]),
        .metallic = static_cast<float>(pbr.metallicFactor),
        .roughness = static_cast<float>(pbr.roughnessFactor),
        .normalScale = static_cast<float>(material.normalTexture.scale),
        .doubleSided = material.doubleSided,
        .baseColorTexture = { pbr.baseColorTexture.index, pbr.baseColorTexture.texCoord },
        .normalTexture = { material.normalTexture.index, material.normalTexture.texCoord } };
}
