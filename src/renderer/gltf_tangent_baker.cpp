#include "renderer/gltf_tangent_baker.hpp"

#include "renderer/gltf_buffer.hpp"
#include "renderer/gltf_primitive.hpp"
#include "renderer/gltf_tangents.hpp"
#include "renderer/gltf_transform.hpp"

#include <cstring>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

class VertexAttributeReader {
public:
    VertexAttributeReader(tinygltf::Model const& model, tinygltf::Accessor const& accessor) {
        if (accessor.sparse.isSparse) {
            throw std::runtime_error("cannot persist tangent seams for sparse vertex attributes");
        }
        auto const& view = model.bufferViews.at(accessor.bufferView);
        int stride = accessor.ByteStride(view);
        int elementSize = tinygltf::GetComponentSizeInBytes(accessor.componentType) *
                          tinygltf::GetNumComponentsInType(accessor.type);
        if (stride < elementSize || elementSize <= 0) {
            throw std::runtime_error("invalid vertex attribute layout while persisting tangents");
        }
        mBuffer = model.buffers.at(view.buffer).data;
        mFirstByte = view.byteOffset + accessor.byteOffset;
        mVertexCount = accessor.count;
        mStride = static_cast<size_t>(stride);
        mElementSize = static_cast<size_t>(elementSize);
    }

    std::vector<uint8_t> copyVertices(std::span<uint32_t const> sources) const {
        std::vector<uint8_t> bytes(sources.size() * mElementSize);
        for (size_t i = 0; i < sources.size(); ++i) {
            auto vertex = vertexBytes(sources[i]);
            memcpy(bytes.data() + i * mElementSize, vertex.data(), vertex.size());
        }
        return bytes;
    }

private:
    std::span<uint8_t const> vertexBytes(uint32_t sourceIndex) const {
        size_t offset = mFirstByte + sourceIndex * mStride;
        if (sourceIndex >= mVertexCount || offset > mBuffer.size() ||
                mElementSize > mBuffer.size() - offset) {
            throw std::runtime_error("vertex attribute is out of bounds while persisting tangents");
        }
        return mBuffer.subspan(offset, mElementSize);
    }

    std::span<uint8_t const> mBuffer;
    size_t mFirstByte = 0;
    size_t mVertexCount = 0;
    size_t mStride = 0;
    size_t mElementSize = 0;
};

template<typename T>
std::span<uint8_t const> bytesOf(std::vector<T> const& values) {
    return { reinterpret_cast<uint8_t const*>(values.data()), values.size() * sizeof(T) };
}

int appendAccessor(tinygltf::Model& model, tinygltf::Accessor accessor,
        std::span<uint8_t const> bytes, int target) {
    accessor.bufferView = gltf::appendBufferView(model, bytes, target);
    accessor.byteOffset = 0;
    model.accessors.push_back(std::move(accessor));
    return static_cast<int>(model.accessors.size() - 1);
}

int duplicateAttribute(tinygltf::Model& model, int accessorIndex,
        std::span<uint32_t const> sources) {
    auto accessor = model.accessors.at(accessorIndex);
    auto bytes = VertexAttributeReader(model, accessor).copyVertices(sources);
    accessor.count = sources.size();
    return appendAccessor(model, std::move(accessor), bytes, TINYGLTF_TARGET_ARRAY_BUFFER);
}

void remapAttributes(tinygltf::Model& model, std::map<std::string, int>& attributes,
        std::span<uint32_t const> sources) {
    for (auto& [name, accessor]: attributes) {
        accessor = duplicateAttribute(model, accessor, sources);
    }
}

void remapPrimitiveAttributes(tinygltf::Model& model, tinygltf::Primitive& primitive,
        std::span<uint32_t const> sources) {
    // Morph targets must follow the same vertex splits as the base attributes.
    remapAttributes(model, primitive.attributes, sources);
    for (auto& target: primitive.targets) {
        remapAttributes(model, target, sources);
    }
}

int appendIndices(tinygltf::Model& model, std::vector<uint32_t> const& indices) {
    tinygltf::Accessor accessor;
    accessor.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
    accessor.type = TINYGLTF_TYPE_SCALAR;
    accessor.count = indices.size();
    return appendAccessor(model, accessor, bytesOf(indices), TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);
}

int appendTangents(tinygltf::Model& model, std::vector<glm::vec4> const& tangents) {
    tinygltf::Accessor accessor;
    accessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    accessor.type = TINYGLTF_TYPE_VEC4;
    accessor.count = tangents.size();
    return appendAccessor(model, accessor, bytesOf(tangents), TINYGLTF_TARGET_ARRAY_BUFFER);
}

void storeTangents(tinygltf::Model& model, tinygltf::Primitive& primitive,
        gltf::PrimitiveGeometry const& geometry) {
    size_t vertexCount = model.accessors.at(primitive.attributes.at("POSITION")).count;
    auto remap =
            gltf::splitTangentSeams(vertexCount, geometry.indices(), geometry.generatedTangents());
    if (remap.sources.size() != vertexCount) {
        remapPrimitiveAttributes(model, primitive, remap.sources);
        primitive.indices = appendIndices(model, remap.indices);
    }
    primitive.attributes["TANGENT"] = appendTangents(model, remap.tangents);
}

bool needsGeneratedTangents(tinygltf::Model const& model, tinygltf::Primitive const& primitive) {
    return !primitive.attributes.contains("TANGENT") && primitive.material >= 0 &&
           model.materials.at(primitive.material).normalTexture.index >= 0;
}

bool bakePrimitiveTangents(tinygltf::Model& model, tinygltf::Primitive& primitive) {
    if (!needsGeneratedTangents(model, primitive)) {
        return false;
    }
    gltf::PrimitiveGeometry geometry(model, primitive);
    if (geometry.generatedTangents().empty()) {
        return false;
    }
    storeTangents(model, primitive, geometry);
    return true;
}

bool bakeMeshTangents(tinygltf::Model& model, tinygltf::Mesh& mesh) {
    bool hasGeneratedTangents = false;
    for (auto& primitive: mesh.primitives) {
        if (bakePrimitiveTangents(model, primitive)) {
            hasGeneratedTangents = true;
        }
    }
    return hasGeneratedTangents;
}

} // namespace

bool gltf::bakeMissingTangents(tinygltf::Model& model) {
    bool hasGeneratedTangents = false;
    std::unordered_set<int> visitedMeshes;
    for (auto const& instance: meshInstances(model)) {
        if (!visitedMeshes.insert(instance.mesh).second) {
            continue;
        }
        if (bakeMeshTangents(model, model.meshes.at(instance.mesh))) {
            hasGeneratedTangents = true;
        }
    }
    if (hasGeneratedTangents) {
        compactGeometryBuffers(model);
    }
    return hasGeneratedTangents;
}
