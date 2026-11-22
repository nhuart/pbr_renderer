#include "renderer/gltf_material.hpp"

#include <stdexcept>
#include <string>
#include <tiny_gltf.h>

namespace {

float readGlossiness(tinygltf::Value const& extension) {
    if (!extension.Has("glossinessFactor")) {
        return 1.0f;
    }
    auto const& glossiness = extension.Get("glossinessFactor");
    if (!glossiness.IsNumber()) {
        throw std::runtime_error("invalid glossiness factor");
    }
    return static_cast<float>(glossiness.GetNumberAsDouble());
}

template<glm::length_t Components>
glm::vec<Components, float> readFactor(tinygltf::Value const& extension, char const* name) {
    glm::vec<Components, float> factor(1.0f);
    if (!extension.Has(name)) {
        return factor;
    }
    auto const& array = extension.Get(name);
    if (!array.IsArray() || array.ArrayLen() != Components) {
        throw std::runtime_error(std::string("invalid specular/glossiness factor: ") + name);
    }
    for (int i = 0; i < Components; ++i) {
        if (!array.Get(i).IsNumber()) {
            throw std::runtime_error(std::string("non-numeric material factor: ") + name);
        }
        factor[i] = static_cast<float>(array.Get(i).GetNumberAsDouble());
    }
    return factor;
}

int readTextureCoordinateSet(tinygltf::Value const& texture) {
    if (!texture.Has("texCoord")) {
        return 0;
    }
    auto const& coordinateSet = texture.Get("texCoord");
    if (!coordinateSet.IsInt()) {
        throw std::runtime_error("invalid texture UV set");
    }
    return coordinateSet.GetNumberAsInt();
}

TextureReference readTexture(tinygltf::Value const& extension, char const* name) {
    if (!extension.Has(name)) {
        return {};
    }
    auto const& texture = extension.Get(name);
    if (!texture.IsObject() || !texture.Has("index") || !texture.Get("index").IsInt()) {
        throw std::runtime_error(std::string("invalid material texture: ") + name);
    }
    return { .texture = texture.Get("index").GetNumberAsInt(),
        .texCoord = readTextureCoordinateSet(texture) };
}

void readSpecularGlossiness(tinygltf::Value const& extension, MaterialDefinition& material) {
    if (!extension.IsObject()) {
        throw std::runtime_error("invalid specular/glossiness material");
    }
    material.workflow = MaterialWorkflow::SpecularGlossiness;
    material.baseColor = readFactor<4>(extension, "diffuseFactor");
    material.specular = readFactor<3>(extension, "specularFactor");
    material.glossiness = readGlossiness(extension);
    material.baseColorTexture = readTexture(extension, "diffuseTexture");
    material.specularGlossinessTexture = readTexture(extension, "specularGlossinessTexture");
}

MaterialDefinition readMetallicRoughness(tinygltf::Material const& material) {
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
    auto definition = readMetallicRoughness(material);
    definition.alphaMode = readAlphaMode(material.alphaMode);
    definition.alphaCutoff = static_cast<float>(material.alphaCutoff);
    auto extension = material.extensions.find("KHR_materials_pbrSpecularGlossiness");
    if (extension != material.extensions.end()) {
        readSpecularGlossiness(extension->second, definition);
    }
    return definition;
}
