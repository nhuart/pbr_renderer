#include "renderer/gltf_material.hpp"

#include <string>
#include <tiny_gltf.h>

namespace {

AlphaMode readAlphaMode(std::string const& alphaMode) {
    if (alphaMode == "MASK") {
        return AlphaMode::Mask;
    }
    if (alphaMode == "BLEND") {
        return AlphaMode::Blend;
    }
    return AlphaMode::Opaque;
}

} // namespace

MaterialDefinition gltf::readMaterial(tinygltf::Material const& material) {
    auto const& pbr = material.pbrMetallicRoughness;
    MaterialDefinition definition{ .baseColor = glm::vec4(pbr.baseColorFactor[0], pbr.baseColorFactor[1],
                     pbr.baseColorFactor[2], pbr.baseColorFactor[3]),
        .metallic = static_cast<float>(pbr.metallicFactor),
        .roughness = static_cast<float>(pbr.roughnessFactor),
        .normalScale = static_cast<float>(material.normalTexture.scale),
        .doubleSided = material.doubleSided,
        .baseColorTexture = { pbr.baseColorTexture.index, pbr.baseColorTexture.texCoord },
        .normalTexture = { material.normalTexture.index, material.normalTexture.texCoord } };
    definition.alphaMode = readAlphaMode(material.alphaMode);
    definition.alphaCutoff = static_cast<float>(material.alphaCutoff);
    return definition;
}
