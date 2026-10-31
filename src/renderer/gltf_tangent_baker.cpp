#include "renderer/gltf_tangent_baker.hpp"

#include "renderer/gltf_primitive.hpp"
#include "renderer/gltf_transform.hpp"

#include <cstring>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace {

struct TangentVertex {
    uint32_t source;
    glm::vec4 tangent;

    bool operator==(TangentVertex const&) const = default;
};

struct TangentVertexHash {
    size_t operator()(TangentVertex const& vertex) const {
        return std::hash<uint32_t>{}(vertex.source) ^ (std::hash<glm::vec4>{}(vertex.tangent) << 1);
    }
};

struct BakedVertices {
    std::vector<uint32_t> sources;
    std::vector<uint32_t> indices;
    std::vector<glm::vec4> tangents;
};

BakedVertices splitTangentSeams(gltf::PrimitiveGeometry const& geometry, size_t vertexCount) {
    BakedVertices baked;
    baked.sources.resize(vertexCount);
    std::iota(baked.sources.begin(), baked.sources.end(), 0);
    baked.tangents.resize(vertexCount, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    baked.indices.reserve(geometry.indexCount());
    std::vector<bool> assigned(vertexCount, false);
    std::unordered_map<TangentVertex, uint32_t, TangentVertexHash> splitVertices;
    for (size_t corner = 0; corner < geometry.indexCount(); ++corner) {
        uint32_t source = geometry.indices()[corner];
        auto const& tangent = geometry.generatedTangents()[corner];
        uint32_t index = source;
        if (!assigned.at(source)) {
            baked.tangents[source] = tangent;
            assigned[source] = true;
        } else if (baked.tangents[source] != tangent) {
            auto [entry, inserted] = splitVertices.try_emplace(TangentVertex{ source, tangent },
                    static_cast<uint32_t>(baked.sources.size()));
            index = entry->second;
            if (inserted) {
                baked.sources.push_back(source);
                baked.tangents.push_back(tangent);
            }
        }
        baked.indices.push_back(index);
    }
    return baked;
}

int appendBufferView(tinygltf::Model& model, std::span<uint8_t const> bytes, int target) {
    auto& buffer = model.buffers.at(0).data;
    // glTF accessors must begin at a four-byte aligned offset in the buffer.
    buffer.resize((buffer.size() + 3) & ~size_t{ 3 });
    tinygltf::BufferView view;
    view.buffer = 0;
    view.byteOffset = buffer.size();
    view.byteLength = bytes.size();
    view.target = target;
    buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    model.bufferViews.push_back(std::move(view));
    return static_cast<int>(model.bufferViews.size() - 1);
}

int appendAccessor(tinygltf::Model& model, tinygltf::Accessor accessor,
        std::span<uint8_t const> bytes, int target) {
    accessor.bufferView = appendBufferView(model, bytes, target);
    accessor.byteOffset = 0;
    model.accessors.push_back(std::move(accessor));
    return static_cast<int>(model.accessors.size() - 1);
}

int duplicateAttribute(tinygltf::Model& model, int accessorIndex,
        std::span<uint32_t const> sources) {
    auto accessor = model.accessors.at(accessorIndex);
    if (accessor.sparse.isSparse) {
        throw std::runtime_error("cannot persist tangent seams for sparse vertex attributes");
    }
    auto const& view = model.bufferViews.at(accessor.bufferView);
    auto const& buffer = model.buffers.at(view.buffer).data;
    int stride = accessor.ByteStride(view);
    int elementSize = tinygltf::GetComponentSizeInBytes(accessor.componentType) *
                      tinygltf::GetNumComponentsInType(accessor.type);
    if (stride < elementSize || elementSize <= 0) {
        throw std::runtime_error("invalid vertex attribute layout while persisting tangents");
    }
    std::vector<uint8_t> bytes(sources.size() * elementSize);
    for (size_t i = 0; i < sources.size(); ++i) {
        size_t offset = view.byteOffset + accessor.byteOffset + sources[i] * size_t(stride);
        if (sources[i] >= accessor.count || offset > buffer.size() ||
                size_t(elementSize) > buffer.size() - offset) {
            throw std::runtime_error("vertex attribute is out of bounds while persisting tangents");
        }
        memcpy(bytes.data() + i * elementSize, buffer.data() + offset, elementSize);
    }
    accessor.count = sources.size();
    return appendAccessor(model, std::move(accessor), bytes, TINYGLTF_TARGET_ARRAY_BUFFER);
}

template<typename T>
std::span<uint8_t const> bytesOf(std::vector<T> const& values) {
    return { reinterpret_cast<uint8_t const*>(values.data()), values.size() * sizeof(T) };
}

void storeTangents(tinygltf::Model& model, tinygltf::Primitive& primitive,
        gltf::PrimitiveGeometry const& geometry) {
    size_t vertexCount = model.accessors.at(primitive.attributes.at("POSITION")).count;
    auto baked = splitTangentSeams(geometry, vertexCount);
    if (baked.sources.size() != vertexCount) {
        // Keep every attribute and morph target aligned with the split source vertices.
        for (auto& [name, accessor]: primitive.attributes) {
            accessor = duplicateAttribute(model, accessor, baked.sources);
        }
        for (auto& target: primitive.targets) {
            for (auto& [name, accessor]: target) {
                accessor = duplicateAttribute(model, accessor, baked.sources);
            }
        }
        tinygltf::Accessor indices;
        indices.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
        indices.type = TINYGLTF_TYPE_SCALAR;
        indices.count = baked.indices.size();
        primitive.indices = appendAccessor(model, indices, bytesOf(baked.indices),
                TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);
    }
    tinygltf::Accessor tangents;
    tangents.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    tangents.type = TINYGLTF_TYPE_VEC4;
    tangents.count = baked.tangents.size();
    primitive.attributes["TANGENT"] =
            appendAccessor(model, tangents, bytesOf(baked.tangents), TINYGLTF_TARGET_ARRAY_BUFFER);
}

template<typename T>
void retainReferenced(std::vector<T>& values, std::vector<int*> const& references) {
    std::vector<int> remap(values.size(), -1);
    std::vector<T> retained;
    for (int* reference: references) {
        if (*reference < 0) {
            continue;
        }
        int& index = remap.at(*reference);
        if (index < 0) {
            index = static_cast<int>(retained.size());
            retained.push_back(std::move(values.at(*reference)));
        }
        *reference = index;
    }
    values = std::move(retained);
}

std::vector<int*> accessorReferences(tinygltf::Model& model) {
    std::vector<int*> references;
    for (auto& mesh: model.meshes) {
        for (auto& primitive: mesh.primitives) {
            references.push_back(&primitive.indices);
            for (auto& [name, accessor]: primitive.attributes) {
                references.push_back(&accessor);
            }
            for (auto& target: primitive.targets) {
                for (auto& [name, accessor]: target) {
                    references.push_back(&accessor);
                }
            }
        }
    }
    for (auto& animation: model.animations) {
        for (auto& sampler: animation.samplers) {
            references.push_back(&sampler.input);
            references.push_back(&sampler.output);
        }
    }
    for (auto& skin: model.skins) {
        references.push_back(&skin.inverseBindMatrices);
    }
    return references;
}

void compactGeometryBuffers(tinygltf::Model& model) {
    // Extensions may contain additional accessor/buffer-view references unknown to TinyGLTF.
    if (!model.extensionsUsed.empty()) {
        return;
    }
    retainReferenced(model.accessors, accessorReferences(model));
    std::vector<int*> views;
    for (auto& accessor: model.accessors) {
        views.push_back(&accessor.bufferView);
        if (accessor.sparse.isSparse) {
            views.push_back(&accessor.sparse.indices.bufferView);
            views.push_back(&accessor.sparse.values.bufferView);
        }
    }
    for (auto& image: model.images) {
        views.push_back(&image.bufferView);
    }
    retainReferenced(model.bufferViews, views);
    std::vector<std::vector<uint8_t>> buffers(model.buffers.size());
    for (auto& view: model.bufferViews) {
        auto const& source = model.buffers.at(view.buffer).data;
        if (view.byteOffset > source.size() || view.byteLength > source.size() - view.byteOffset) {
            throw std::runtime_error("buffer view is out of bounds while persisting tangents");
        }
        auto& buffer = buffers.at(view.buffer);
        buffer.resize((buffer.size() + 3) & ~size_t{ 3 });
        size_t offset = buffer.size();
        buffer.insert(buffer.end(), source.begin() + view.byteOffset,
                source.begin() + view.byteOffset + view.byteLength);
        view.byteOffset = offset;
    }
    for (size_t i = 0; i < model.buffers.size(); ++i) {
        model.buffers[i].data = std::move(buffers[i]);
    }
}

} // namespace

bool gltf::bakeMissingTangents(tinygltf::Model& model) {
    bool changed = false;
    std::unordered_set<int> visitedMeshes;
    for (auto const& instance: meshInstances(model)) {
        if (!visitedMeshes.insert(instance.mesh).second) {
            continue;
        }
        for (auto& primitive: model.meshes.at(instance.mesh).primitives) {
            if (primitive.attributes.contains("TANGENT") || primitive.material < 0 ||
                    model.materials.at(primitive.material).normalTexture.index < 0) {
                continue;
            }
            PrimitiveGeometry geometry(model, primitive);
            if (!geometry.generatedTangents().empty()) {
                storeTangents(model, primitive, geometry);
                changed = true;
            }
        }
    }
    if (changed) {
        compactGeometryBuffers(model);
    }
    return changed;
}
