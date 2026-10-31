#include "renderer/gltf_primitive.hpp"

#include <cstring>
#include <numeric>
#include <stdexcept>
#include <string>

#include <tiny_gltf.h>

#include "renderer/gltf_tangents.hpp"

namespace {

uint8_t const* accessorData(tinygltf::Model const& model, tinygltf::Accessor const& accessor) {
    auto const& bufferView = model.bufferViews[accessor.bufferView];
    return model.buffers[bufferView.buffer].data.data() + bufferView.byteOffset +
           accessor.byteOffset;
}

template<typename T>
T readAt(uint8_t const* bytes, size_t index) {
    T value{};
    memcpy(&value, bytes + index * sizeof(T), sizeof(T));
    return value;
}

template<glm::length_t Components>
class FloatAttribute {
public:
    FloatAttribute(tinygltf::Model const& model, tinygltf::Primitive const& primitive,
            char const* name) {
        auto attribute = primitive.attributes.find(name);
        if (attribute == primitive.attributes.end()) {
            return;
        }
        auto const& accessor = model.accessors[attribute->second];
        auto const& bufferView = model.bufferViews[accessor.bufferView];
        mBytes = accessorData(model, accessor);
        mStride = bufferView.byteStride ? bufferView.byteStride : Components * sizeof(float);
    }

    bool hasValues() const { return mBytes != nullptr; }

    glm::vec<Components, float> read(size_t index) const {
        glm::vec<Components, float> value(0.0f);
        if (!hasValues()) {
            return value;
        }
        for (glm::length_t component = 0; component < Components; ++component) {
            value[component] = readAt<float>(mBytes + index * mStride, component);
        }
        return value;
    }

private:
    uint8_t const* mBytes = nullptr;
    size_t mStride = 0;
};

tinygltf::NormalTextureInfo const* normalTexture(tinygltf::Model const& model,
        tinygltf::Primitive const& primitive) {
    if (primitive.material < 0) {
        return nullptr;
    }
    auto const& texture = model.materials.at(primitive.material).normalTexture;
    return texture.index >= 0 ? &texture : nullptr;
}

std::vector<Vertex> readPrimitiveVertices(tinygltf::Model const& model,
        tinygltf::Primitive const& primitive) {
    auto const* normalMap = normalTexture(model, primitive);
    std::string normalUVAttribute =
            "TEXCOORD_" + std::to_string(normalMap ? normalMap->texCoord : 0);
    FloatAttribute<3> positions(model, primitive, "POSITION");
    auto const* baseColorMap =
            primitive.material >= 0
                    ? &model.materials.at(primitive.material).pbrMetallicRoughness.baseColorTexture
                    : nullptr;
    int baseColorUV = baseColorMap ? baseColorMap->texCoord : 0;
    std::string baseColorUVAttribute = "TEXCOORD_" + std::to_string(baseColorUV);
    FloatAttribute<2> texCoords(model, primitive, baseColorUVAttribute.c_str());
    FloatAttribute<2> normalTexCoords(model, primitive, normalUVAttribute.c_str());
    FloatAttribute<3> normals(model, primitive, "NORMAL");
    FloatAttribute<4> tangents(model, primitive, "TANGENT");
    if (baseColorMap && baseColorMap->index >= 0 && !texCoords.hasValues()) {
        throw std::runtime_error("base-color mapping requires " + baseColorUVAttribute);
    }
    if (normalMap && (!normalTexCoords.hasValues() || !normals.hasValues())) {
        throw std::runtime_error("normal mapping requires normals and " + normalUVAttribute);
    }
    size_t vertexCount = model.accessors[primitive.attributes.at("POSITION")].count;
    std::vector<Vertex> vertices(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        vertices[i] = { .pos = positions.read(i),
            .color = glm::vec3(1.0f),
            .texCoord = texCoords.read(i),
            .normal = normals.read(i),
            .tangent = tangents.read(i),
            .normalTexCoord = normalTexCoords.read(i) };
    }
    return vertices;
}

uint32_t readPrimitiveIndex(uint8_t const* bytes, size_t index, int componentType) {
    switch (componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return readAt<uint16_t>(bytes, index);
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            return readAt<uint32_t>(bytes, index);
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return readAt<uint8_t>(bytes, index);
        default:
            throw std::runtime_error("unsupported glTF index component type");
    }
}

std::vector<uint32_t> readPrimitiveIndices(tinygltf::Model const& model,
        tinygltf::Primitive const& primitive) {
    if (primitive.indices < 0) {
        size_t vertexCount = model.accessors[primitive.attributes.at("POSITION")].count;
        std::vector<uint32_t> indices(vertexCount);
        std::iota(indices.begin(), indices.end(), 0);
        return indices;
    }
    auto const& accessor = model.accessors[primitive.indices];
    auto const* bytes = accessorData(model, accessor);
    std::vector<uint32_t> indices(accessor.count);
    for (size_t i = 0; i < accessor.count; ++i) {
        indices[i] = readPrimitiveIndex(bytes, i, accessor.componentType);
    }
    return indices;
}

} // namespace

gltf::PrimitiveGeometry::PrimitiveGeometry(tinygltf::Model const& model,
        tinygltf::Primitive const& primitive) {
    if (primitive.mode != TINYGLTF_MODE_TRIANGLES && primitive.mode != -1) {
        throw std::runtime_error("only glTF triangle primitives are supported");
    }
    mVertices = readPrimitiveVertices(model, primitive);
    mIndices = readPrimitiveIndices(model, primitive);
    // MikkTSpace must see the original glTF geometry and UVs, before sampling flips or transforms.
    generateMissingTangents(model, primitive);
    flipTextureCoordinates();
}

void gltf::PrimitiveGeometry::generateMissingTangents(tinygltf::Model const& model,
        tinygltf::Primitive const& primitive) {
    if (primitive.attributes.contains("TANGENT") || !normalTexture(model, primitive)) {
        return;
    }
    mCornerTangents = generateTangents(mVertices, mIndices);
}

void gltf::PrimitiveGeometry::flipTextureCoordinates() {
    for (auto& vertex: mVertices) {
        vertex.texCoord.y = 1.0f - vertex.texCoord.y;
        vertex.normalTexCoord.y = 1.0f - vertex.normalTexCoord.y;
    }
}

Vertex gltf::PrimitiveGeometry::vertexAtCorner(size_t corner) const {
    Vertex vertex = mVertices.at(mIndices.at(corner));
    // Tangents belong to corners, not source indices: mirrored UV seams must remain distinct.
    if (!mCornerTangents.empty()) {
        vertex.tangent = mCornerTangents.at(corner);
    }
    return vertex;
}
