#include "renderer/gltf_primitive.hpp"
#include "renderer/gltf_material.hpp"

#include <cstring>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

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
            std::string const& name) {
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

bool hasNormalizedColorComponents(tinygltf::Accessor const& accessor) {
    return accessor.normalized &&
           (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
                   accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
}

void validateColorAccessor(tinygltf::Accessor const& accessor) {
    if (accessor.type != TINYGLTF_TYPE_VEC3 && accessor.type != TINYGLTF_TYPE_VEC4) {
        throw std::runtime_error("vertex colors require VEC3 or VEC4");
    }
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT &&
            !hasNormalizedColorComponents(accessor)) {
        throw std::runtime_error("vertex colors require floats or normalized unsigned integers");
    }
}

float readColorComponent(uint8_t const* bytes, int component, int componentType) {
    switch (componentType) {
        case TINYGLTF_COMPONENT_TYPE_FLOAT:
            return readAt<float>(bytes, component);
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return readAt<uint8_t>(bytes, component) / 255.0f;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return readAt<uint16_t>(bytes, component) / 65535.0f;
        default:
            throw std::runtime_error("unsupported vertex color component type");
    }
}

class ColorAttribute {
public:
    ColorAttribute(tinygltf::Model const& model, tinygltf::Primitive const& primitive) {
        auto entry = primitive.attributes.find("COLOR_0");
        if (entry == primitive.attributes.end()) {
            return;
        }
        auto const& accessor = model.accessors.at(entry->second);
        validateColorAccessor(accessor);
        mComponentType = accessor.componentType;
        mComponents = accessor.type == TINYGLTF_TYPE_VEC4 ? 4 : 3;
        mBytes = accessorData(model, accessor);
        mStride = accessor.ByteStride(model.bufferViews.at(accessor.bufferView));
    }

    glm::vec4 read(size_t index) const {
        glm::vec4 color(1.0f);
        if (!mBytes) {
            return color;
        }
        auto const* bytes = mBytes + index * mStride;
        for (int component = 0; component < mComponents; ++component) {
            color[component] = readColorComponent(bytes, component, mComponentType);
        }
        return color;
    }

private:
    uint8_t const* mBytes = nullptr;
    size_t mStride = 0;
    int mComponents = 0;
    int mComponentType = 0;
};

tinygltf::NormalTextureInfo const* normalTexture(tinygltf::Model const& model,
        tinygltf::Primitive const& primitive) {
    if (primitive.material < 0) {
        return nullptr;
    }
    auto const& texture = model.materials.at(primitive.material).normalTexture;
    return texture.index >= 0 ? &texture : nullptr;
}

class TextureCoordinates {
public:
    TextureCoordinates(tinygltf::Model const& model, tinygltf::Primitive const& primitive,
            TextureReference reference)
            : mAttributeName("TEXCOORD_" + std::to_string(reference.texCoord)),
              mValues(model, primitive, mAttributeName) {}

    void requireValues(TextureReference reference, char const* purpose) const {
        if (reference.hasTexture() && !mValues.hasValues()) {
            throw std::runtime_error(std::string(purpose) + " mapping requires " + mAttributeName);
        }
    }

    void requireNormalMapping(TextureReference reference, bool hasNormals) const {
        if (reference.hasTexture() && (!mValues.hasValues() || !hasNormals)) {
            throw std::runtime_error("normal mapping requires normals and " + mAttributeName);
        }
    }

    glm::vec2 read(size_t index) const { return mValues.read(index); }

private:
    std::string mAttributeName;
    FloatAttribute<2> mValues;
};

class PrimitiveVertexReader {
public:
    PrimitiveVertexReader(tinygltf::Model const& model, tinygltf::Primitive const& primitive,
            MaterialDefinition const& material)
            : mPositions(model, primitive, "POSITION"),
              mNormals(model, primitive, "NORMAL"),
              mTangents(model, primitive, "TANGENT"),
              mColors(model, primitive),
              mBaseColorUVs(model, primitive, material.baseColorTexture),
              mNormalUVs(model, primitive, material.normalTexture),
              mSpecularGlossinessUVs(model, primitive, material.specularGlossinessTexture) {
        mBaseColorUVs.requireValues(material.baseColorTexture, "base-color");
        mSpecularGlossinessUVs.requireValues(material.specularGlossinessTexture,
                "specular/glossiness");
        mNormalUVs.requireNormalMapping(material.normalTexture, mNormals.hasValues());
    }

    Vertex read(size_t index) const {
        return { .pos = mPositions.read(index),
            .color = mColors.read(index),
            .texCoord = mBaseColorUVs.read(index),
            .normal = mNormals.read(index),
            .tangent = mTangents.read(index),
            .normalTexCoord = mNormalUVs.read(index),
            .specularTexCoord = mSpecularGlossinessUVs.read(index) };
    }

private:
    FloatAttribute<3> mPositions;
    FloatAttribute<3> mNormals;
    FloatAttribute<4> mTangents;
    ColorAttribute mColors;
    TextureCoordinates mBaseColorUVs;
    TextureCoordinates mNormalUVs;
    TextureCoordinates mSpecularGlossinessUVs;
};

std::vector<Vertex> readPrimitiveVertices(tinygltf::Model const& model,
        tinygltf::Primitive const& primitive) {
    auto material = primitive.material >= 0
                            ? gltf::readMaterial(model.materials.at(primitive.material))
                            : MaterialDefinition{};
    PrimitiveVertexReader reader(model, primitive, material);
    size_t vertexCount = model.accessors[primitive.attributes.at("POSITION")].count;
    std::vector<Vertex> vertices(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        vertices[i] = reader.read(i);
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
        vertex.specularTexCoord.y = 1.0f - vertex.specularTexCoord.y;
    }
}

void gltf::PrimitiveGeometry::indexGeneratedTangents() {
    if (mCornerTangents.empty()) {
        return;
    }
    auto remap = splitTangentSeams(mVertices.size(), mIndices, mCornerTangents);
    mVertices.reserve(remap.sources.size());
    for (size_t i = mVertices.size(); i < remap.sources.size(); ++i) {
        mVertices.push_back(mVertices.at(remap.sources[i]));
    }
    for (size_t i = 0; i < mVertices.size(); ++i) {
        mVertices[i].tangent = remap.tangents[i];
    }
    mIndices = std::move(remap.indices);
    mCornerTangents.clear();
}
