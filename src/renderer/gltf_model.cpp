#include "renderer/gltf_model.hpp"
#include "renderer/gltf_buffer.hpp"
#include "renderer/gltf_model_io.hpp"
#include "renderer/gltf_primitive.hpp"
#include "renderer/gltf_tangent_baker.hpp"
#include "renderer/gltf_transform.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include <tiny_gltf.h>

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

EmbeddedImage readEncodedImage(tinygltf::Model const& model, tinygltf::Image const& image) {
    if (image.bufferView < 0) {
        return EmbeddedImage(image.image);
    }
    auto bytes = gltf::bufferViewData(model, model.bufferViews.at(image.bufferView));
    return EmbeddedImage(std::vector<uint8_t>(bytes.begin(), bytes.end()));
}

EmbeddedImage readDecodedImage(tinygltf::Image const& image) {
    if (image.bits != 8 || image.component != 4 || image.width <= 0 || image.height <= 0 ||
            image.image.size() != static_cast<size_t>(image.width) * image.height * 4) {
        throw std::runtime_error("embedded glTF textures require decoded RGBA8 images");
    }
    return EmbeddedImage::fromPixels(image.image, static_cast<uint32_t>(image.width),
            static_cast<uint32_t>(image.height));
}

EmbeddedImage readImage(tinygltf::Model const& model, int imageIndex) {
    auto const& image = model.images.at(imageIndex);
    return image.as_is ? readEncodedImage(model, image) : readDecodedImage(image);
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

void readReferencedImage(tinygltf::Model const& model, ModelAsset& asset,
        TextureReference reference) {
    if (!reference.hasTexture()) {
        return;
    }
    int imageIndex = asset.textures.at(reference.texture).image;
    auto& image = asset.images.at(imageIndex);
    if (image.empty()) {
        image = readImage(model, imageIndex);
    }
}

void readImages(tinygltf::Model const& model, ModelAsset& asset) {
    asset.images.resize(model.images.size());
    for (auto const& material: asset.materials) {
        readReferencedImage(model, asset, material.baseColorTexture);
        readReferencedImage(model, asset, material.normalTexture);
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
        geometry.indexGeneratedTangents();
        gltf::VertexTransform vertexTransform(transform);
        IndexRange range{ static_cast<uint32_t>(mOutput.indices.size()),
            static_cast<uint32_t>(geometry.indexCount()) };
        appendIndexedVertices(geometry, vertexTransform);
        if (vertexTransform.reversesWinding()) {
            reverseWinding(range);
        }
        mOutput.asset.primitives.push_back({ range, resolveMaterialIndex(model, primitive) });
    }

private:
    void appendIndexedVertices(gltf::PrimitiveGeometry const& geometry,
            gltf::VertexTransform const& transform) {
        constexpr uint32_t UNMAPPED_VERTEX = std::numeric_limits<uint32_t>::max();
        std::vector<uint32_t> remap(geometry.vertices().size(), UNMAPPED_VERTEX);
        for (uint32_t sourceIndex: geometry.indices()) {
            auto& index = remap.at(sourceIndex);
            if (index == UNMAPPED_VERTEX) {
                auto vertex = geometry.vertices()[sourceIndex];
                transform.apply(vertex);
                index = static_cast<uint32_t>(mOutput.vertices.size());
                mOutput.vertices.push_back(vertex);
            }
            mOutput.indices.push_back(index);
        }
    }

    void reverseWinding(IndexRange range) {
        size_t end = static_cast<size_t>(range.firstIndex) + range.indexCount;
        for (size_t i = range.firstIndex; i + 2 < end; i += 3) {
            std::swap(mOutput.indices[i + 1], mOutput.indices[i + 2]);
        }
    }

    gltf::ImportedModel& mOutput;
};

void reserveGeometry(tinygltf::Model const& model, std::vector<gltf::MeshInstance> const& instances,
        gltf::ImportedModel& output) {
    size_t vertexCapacity = 0;
    size_t indexCapacity = 0;
    for (auto const& instance: instances) {
        for (auto const& primitive: model.meshes.at(instance.mesh).primitives) {
            size_t vertexCount = model.accessors.at(primitive.attributes.at("POSITION")).count;
            size_t indexCount = primitive.indices < 0 ? vertexCount
                                                      : model.accessors.at(primitive.indices).count;
            vertexCapacity += std::min(vertexCount, indexCount);
            indexCapacity += indexCount;
        }
    }
    output.vertices.reserve(vertexCapacity);
    output.indices.reserve(indexCapacity);
}

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
    auto instances = meshInstances(model);
    reserveGeometry(model, instances, output);
    ModelGeometryBuilder geometry(output);
    for (auto const& instance: instances) {
        for (auto const& primitive: model.meshes.at(instance.mesh).primitives) {
            geometry.append(model, primitive, instance.transform);
        }
    }
    output.asset.range.indexCount = static_cast<uint32_t>(output.indices.size());
    calculateBounds(output);
    return output;
}

gltf::ImportedModel gltf::loadModel(std::string const& path) {
    auto model = readBinaryModel(path);
    bool hasGeneratedTangents = bakeMissingTangents(model);
    auto imported = importModel(model);
    if (hasGeneratedTangents) {
        saveGeneratedTangents(model, path);
    }
    return imported;
}
