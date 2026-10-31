#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>
#undef TINYGLTF_IMPLEMENTATION
#undef STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_WRITE_IMPLEMENTATION

#include "renderer/gltf_model.hpp"
#include "renderer/gltf_primitive.hpp"
#include "renderer/gltf_transform.hpp"

#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace {

vk::SamplerAddressMode wrapMode(int mode) {
    switch (mode) {
        case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
            return vk::SamplerAddressMode::eClampToEdge;
        case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
            return vk::SamplerAddressMode::eMirroredRepeat;
        default:
            return vk::SamplerAddressMode::eRepeat;
    }
}

TextureSampler readSampler(tinygltf::Sampler const& sampler) {
    bool nearestMin = sampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST ||
                      sampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST ||
                      sampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR;
    bool nearestMip = sampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST ||
                      sampler.minFilter == TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST;
    return { .minFilter = nearestMin ? vk::Filter::eNearest : vk::Filter::eLinear,
        .magFilter = sampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST ? vk::Filter::eNearest
                                                                          : vk::Filter::eLinear,
        .mipmapMode = nearestMip ? vk::SamplerMipmapMode::eNearest : vk::SamplerMipmapMode::eLinear,
        .wrapU = wrapMode(sampler.wrapS),
        .wrapV = wrapMode(sampler.wrapT) };
}

MaterialDefinition readMaterial(tinygltf::Material const& material) {
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

EmbeddedImage readImage(tinygltf::Image const& image) {
    if (image.bits != 8 || image.component != 4 || image.width <= 0 || image.height <= 0 ||
            image.image.size() != static_cast<size_t>(image.width) * image.height * 4) {
        throw std::runtime_error("embedded glTF textures require decoded RGBA8 images");
    }
    EmbeddedImage output{ .pixels = std::vector<uint8_t>(image.image.size()),
        .width = static_cast<uint32_t>(image.width),
        .height = static_cast<uint32_t>(image.height) };
    size_t rowBytes = output.width * 4;
    for (uint32_t row = 0; row < output.height; ++row) {
        memcpy(output.pixels.data() + row * rowBytes,
                image.image.data() + (output.height - 1 - row) * rowBytes, rowBytes);
    }
    return output;
}

void readMaterials(tinygltf::Model const& model, ModelAsset& asset) {
    for (auto const& material: model.materials) {
        asset.materials.push_back(readMaterial(material));
    }
    // Normalize omitted primitive materials to a valid, shared default entry.
    asset.materials.emplace_back();
}

void readTextures(tinygltf::Model const& model, ModelAsset& asset) {
    for (auto const& texture: model.textures) {
        asset.textures.push_back({ .image = texture.source,
            .sampler = texture.sampler >= 0 ? readSampler(model.samplers.at(texture.sampler))
                                            : TextureSampler{} });
    }
}

void readImages(tinygltf::Model const& model, ModelAsset& asset) {
    asset.images.resize(model.images.size());
    for (auto const& material: asset.materials) {
        for (auto reference: { material.baseColorTexture, material.normalTexture }) {
            if (!reference.hasTexture()) {
                continue;
            }
            int imageIndex = asset.textures.at(reference.texture).image;
            auto& image = asset.images.at(imageIndex);
            if (image.pixels.empty()) {
                image = readImage(model.images.at(imageIndex));
            }
        }
    }
}

uint32_t resolveMaterialIndex(tinygltf::Model const& model, tinygltf::Primitive const& primitive) {
    if (primitive.material >= 0 &&
            static_cast<size_t>(primitive.material) >= model.materials.size()) {
        throw std::runtime_error("glTF primitive material index is out of range");
    }
    return primitive.material >= 0 ? static_cast<uint32_t>(primitive.material)
                                   : static_cast<uint32_t>(model.materials.size());
}

class ModelGeometryBuilder {
public:
    explicit ModelGeometryBuilder(gltf::ImportedModel& output)
            : mOutput(output) {}

    void append(tinygltf::Model const& model, tinygltf::Primitive const& primitive,
            glm::mat4 const& transform) {
        gltf::PrimitiveGeometry geometry(model, primitive);
        gltf::VertexTransform vertexTransform(transform);
        IndexRange range{ static_cast<uint32_t>(mOutput.indices.size()),
            static_cast<uint32_t>(geometry.indexCount()) };
        appendCorners(geometry, vertexTransform);
        if (vertexTransform.reversesWinding()) {
            reverseWinding(range);
        }
        mOutput.asset.primitives.push_back({ range, resolveMaterialIndex(model, primitive) });
    }

private:
    void appendCorners(gltf::PrimitiveGeometry const& geometry,
            gltf::VertexTransform const& transform) {
        for (size_t corner = 0; corner < geometry.indexCount(); ++corner) {
            auto vertex = geometry.vertexAtCorner(corner);
            transform.apply(vertex);
            auto [entry, inserted] =
                    mUniqueVertices.emplace(vertex, static_cast<uint32_t>(mOutput.vertices.size()));
            if (inserted) {
                mOutput.vertices.push_back(vertex);
            }
            mOutput.indices.push_back(entry->second);
        }
    }

    void reverseWinding(IndexRange range) {
        size_t end = static_cast<size_t>(range.firstIndex) + range.indexCount;
        for (size_t i = range.firstIndex; i + 2 < end; i += 3) {
            std::swap(mOutput.indices[i + 1], mOutput.indices[i + 2]);
        }
    }

    gltf::ImportedModel& mOutput;
    std::unordered_map<Vertex, uint32_t> mUniqueVertices;
};

void calculateBounds(gltf::ImportedModel& model) {
    if (model.vertices.empty()) {
        return;
    }
    model.asset.boundsMin = glm::vec3(std::numeric_limits<float>::max());
    model.asset.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
    for (auto const& vertex: model.vertices) {
        model.asset.boundsMin = glm::min(model.asset.boundsMin, vertex.pos);
        model.asset.boundsMax = glm::max(model.asset.boundsMax, vertex.pos);
    }
}

} // namespace

gltf::ImportedModel gltf::importModel(tinygltf::Model const& model) {
    ImportedModel output;
    readMaterials(model, output.asset);
    readTextures(model, output.asset);
    readImages(model, output.asset);
    ModelGeometryBuilder geometry(output);
    for (auto const& instance: meshInstances(model)) {
        for (auto const& primitive: model.meshes.at(instance.mesh).primitives) {
            geometry.append(model, primitive, instance.transform);
        }
    }
    output.asset.range.indexCount = static_cast<uint32_t>(output.indices.size());
    calculateBounds(output);
    return output;
}

gltf::ImportedModel gltf::loadModel(std::string const& path) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string warning;
    std::string error;
    bool loaded = loader.LoadBinaryFromFile(&model, &error, &warning, path);
    if (!warning.empty()) {
        std::cerr << "glTF warning: " << warning << '\n';
    }
    if (!loaded) {
        throw std::runtime_error("failed to load glTF model (" + path + "): " + error);
    }
    return importModel(model);
}
