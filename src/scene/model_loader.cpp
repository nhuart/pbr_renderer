#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include "core/application.hpp"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

uint8_t const* Renderer::accessorData(tinygltf::Model const& model, tinygltf::Accessor const& acc) {
    auto const& bufView = model.bufferViews[acc.bufferView];
    return model.buffers[bufView.buffer].data.data() + bufView.byteOffset + acc.byteOffset;
}

void Renderer::loadPrimitive(tinygltf::Model const& model, tinygltf::Primitive const& primitive,
        std::unordered_map<Vertex, uint32_t>& uniqueVertices) {
    auto const& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
    auto const& posView = model.bufferViews[posAccessor.bufferView];
    uint8_t const* posBytes = accessorData(model, posAccessor);
    size_t posStride = posView.byteStride ? posView.byteStride : 3 * sizeof(float);

    uint8_t const* uvBytes = nullptr;
    size_t uvStride = 2 * sizeof(float);
    if (primitive.attributes.contains("TEXCOORD_0")) {
        auto const& uvAccessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
        auto const& uvView = model.bufferViews[uvAccessor.bufferView];
        uvBytes = accessorData(model, uvAccessor);
        uvStride = uvView.byteStride ? uvView.byteStride : 2 * sizeof(float);
    }

    size_t vertexCount = posAccessor.count;
    std::vector<uint32_t> localRemap(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        Vertex vertex{};
        float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
        memcpy(&posX, posBytes + i * posStride + 0 * sizeof(float), sizeof(float));
        memcpy(&posY, posBytes + i * posStride + 1 * sizeof(float), sizeof(float));
        memcpy(&posZ, posBytes + i * posStride + 2 * sizeof(float), sizeof(float));
        vertex.pos = { posX, posY, posZ };
        vertex.color = { 1.0f, 1.0f, 1.0f };
        if (uvBytes != nullptr) {
            float uvU = 0.0f, uvV = 0.0f;
            memcpy(&uvU, uvBytes + i * uvStride + 0 * sizeof(float), sizeof(float));
            memcpy(&uvV, uvBytes + i * uvStride + 1 * sizeof(float), sizeof(float));
            // glTF UV origin is top-left (OpenGL convention); flip V for Vulkan.
            vertex.texCoord = { uvU, 1.0f - uvV };
        }
        auto [it, inserted] =
                uniqueVertices.insert({ vertex, static_cast<uint32_t>(vertices.size()) });
        if (inserted) {
            vertices.push_back(vertex);
        }
        localRemap[i] = it->second;
    }

    auto const& idxAccessor = model.accessors[primitive.indices];
    uint8_t const* rawIdx = accessorData(model, idxAccessor);
    for (size_t i = 0; i < idxAccessor.count; ++i) {
        uint32_t rawIndex = 0;
        if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            rawIndex = readAt<uint16_t>(rawIdx, i);
        } else if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            rawIndex = readAt<uint32_t>(rawIdx, i);
        } else if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            rawIndex = readAt<uint8_t>(rawIdx, i);
        }
        indices.push_back(localRemap[rawIndex]);
    }
}

void Renderer::loadModel() {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string warn;
    std::string err;

    bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, MODEL_PATH);
    if (!warn.empty()) {
        std::cerr << "glTF warning: " << warn << "\n";
    }
    if (!ret) {
        throw std::runtime_error("failed to load glTF model: " + err);
    }

    std::unordered_map<Vertex, uint32_t> uniqueVertices;
    for (auto const& mesh: model.meshes) {
        for (auto const& primitive: mesh.primitives) {
            loadPrimitive(model, primitive, uniqueVertices);
        }
    }

    std::cout << "Model loaded: " << vertices.size() << " unique vertices, " << indices.size()
              << " indices\n";
}
