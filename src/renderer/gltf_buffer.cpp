#include "renderer/gltf_buffer.hpp"

#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <tiny_gltf.h>

namespace {

using IndexReferences = std::vector<int*>;

size_t appendAlignedBytes(std::vector<uint8_t>& buffer, std::span<uint8_t const> bytes) {
    constexpr size_t BUFFER_ALIGNMENT = 4;
    buffer.resize((buffer.size() + BUFFER_ALIGNMENT - 1) & ~(BUFFER_ALIGNMENT - 1));
    size_t offset = buffer.size();
    buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    return offset;
}

template<typename T>
void retainReferenced(std::vector<T>& values, IndexReferences const& references) {
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

void collectAttributeReferences(std::map<std::string, int>& attributes,
        IndexReferences& references) {
    for (auto& [name, accessor]: attributes) {
        references.push_back(&accessor);
    }
}

void collectPrimitiveReferences(tinygltf::Primitive& primitive, IndexReferences& references) {
    references.push_back(&primitive.indices);
    collectAttributeReferences(primitive.attributes, references);
    for (auto& target: primitive.targets) {
        collectAttributeReferences(target, references);
    }
}

void collectAnimationReferences(tinygltf::Animation& animation, IndexReferences& references) {
    for (auto& sampler: animation.samplers) {
        references.push_back(&sampler.input);
        references.push_back(&sampler.output);
    }
}

IndexReferences accessorReferences(tinygltf::Model& model) {
    IndexReferences references;
    for (auto& mesh: model.meshes) {
        for (auto& primitive: mesh.primitives) {
            collectPrimitiveReferences(primitive, references);
        }
    }
    for (auto& animation: model.animations) {
        collectAnimationReferences(animation, references);
    }
    for (auto& skin: model.skins) {
        references.push_back(&skin.inverseBindMatrices);
    }
    return references;
}

IndexReferences bufferViewReferences(tinygltf::Model& model) {
    IndexReferences references;
    for (auto& accessor: model.accessors) {
        references.push_back(&accessor.bufferView);
        if (accessor.sparse.isSparse) {
            references.push_back(&accessor.sparse.indices.bufferView);
            references.push_back(&accessor.sparse.values.bufferView);
        }
    }
    for (auto& image: model.images) {
        references.push_back(&image.bufferView);
    }
    return references;
}

void repackBuffers(tinygltf::Model& model) {
    std::vector<std::vector<uint8_t>> compactedBuffers(model.buffers.size());
    for (auto& view: model.bufferViews) {
        auto bytes = gltf::bufferViewData(model, view);
        view.byteOffset = appendAlignedBytes(compactedBuffers.at(view.buffer), bytes);
    }
    for (size_t i = 0; i < model.buffers.size(); ++i) {
        model.buffers[i].data = std::move(compactedBuffers[i]);
    }
}

} // namespace

std::span<uint8_t const> gltf::bufferViewData(tinygltf::Model const& model,
        tinygltf::BufferView const& view) {
    auto const& buffer = model.buffers.at(view.buffer).data;
    if (view.byteOffset > buffer.size() || view.byteLength > buffer.size() - view.byteOffset) {
        throw std::runtime_error("glTF buffer view is out of bounds");
    }
    return std::span<uint8_t const>(buffer).subspan(view.byteOffset, view.byteLength);
}

int gltf::appendBufferView(tinygltf::Model& model, std::span<uint8_t const> bytes, int target) {
    tinygltf::BufferView view;
    view.buffer = 0;
    view.byteOffset = appendAlignedBytes(model.buffers.at(0).data, bytes);
    view.byteLength = bytes.size();
    view.target = target;
    model.bufferViews.push_back(std::move(view));
    return static_cast<int>(model.bufferViews.size() - 1);
}

void gltf::compactGeometryBuffers(tinygltf::Model& model) {
    // Extensions may contain additional accessor/buffer-view references unknown to TinyGLTF.
    if (!model.extensionsUsed.empty()) {
        return;
    }
    retainReferenced(model.accessors, accessorReferences(model));
    retainReferenced(model.bufferViews, bufferViewReferences(model));
    repackBuffers(model);
}
