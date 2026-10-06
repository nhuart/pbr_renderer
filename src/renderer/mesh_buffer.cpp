#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include "core/command_service.hpp"
#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "renderer/mesh_buffer.hpp"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {

std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> uploadDeviceBuffer(VulkanContext const& ctx,
        CommandService const& cmds, void const* source, vk::DeviceSize size,
        vk::BufferUsageFlagBits usage) {
    auto [stagingBuffer, stagingMemory] = vkutil::createBuffer(ctx, size,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    void* mapped = stagingMemory.mapMemory(0, size);
    memcpy(mapped, source, static_cast<size_t>(size));
    stagingMemory.unmapMemory();

    auto [deviceBuffer, deviceMemory] =
            vkutil::createBuffer(ctx, size, usage | vk::BufferUsageFlagBits::eTransferDst,
                    vk::MemoryPropertyFlagBits::eDeviceLocal);
    vkutil::copyBuffer(ctx, cmds.commandPool, stagingBuffer, deviceBuffer, size);
    return { std::move(deviceBuffer), std::move(deviceMemory) };
}

} // namespace

MeshBuffer::MeshBuffer(VulkanContext const& ctx, CommandService const& cmds, Scene const& scene) {
    loadMeshes(scene);
    uploadBuffers(ctx, cmds);
}

uint8_t const* MeshBuffer::accessorData(tinygltf::Model const& model,
        tinygltf::Accessor const& acc) {
    auto const& bufView = model.bufferViews[acc.bufferView];
    return model.buffers[bufView.buffer].data.data() + bufView.byteOffset + acc.byteOffset;
}

void MeshBuffer::loadPrimitive(tinygltf::Model const& model, tinygltf::Primitive const& primitive,
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

    uint8_t const* normalBytes = nullptr;
    size_t normalStride = 3 * sizeof(float);
    if (primitive.attributes.contains("NORMAL")) {
        auto const& normalAccessor = model.accessors[primitive.attributes.at("NORMAL")];
        auto const& normalView = model.bufferViews[normalAccessor.bufferView];
        normalBytes = accessorData(model, normalAccessor);
        normalStride = normalView.byteStride ? normalView.byteStride : 3 * sizeof(float);
    }

    uint8_t const* tangentBytes = nullptr;
    size_t tangentStride = 4 * sizeof(float);
    if (primitive.attributes.contains("TANGENT")) {
        auto const& tangentAccessor = model.accessors[primitive.attributes.at("TANGENT")];
        auto const& tangentView = model.bufferViews[tangentAccessor.bufferView];
        tangentBytes = accessorData(model, tangentAccessor);
        tangentStride = tangentView.byteStride ? tangentView.byteStride : 4 * sizeof(float);
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
            vertex.texCoord = { uvU, 1.0f - uvV };
        }
        if (normalBytes != nullptr) {
            memcpy(&vertex.normal.x, normalBytes + i * normalStride + 0 * sizeof(float),
                    sizeof(float));
            memcpy(&vertex.normal.y, normalBytes + i * normalStride + 1 * sizeof(float),
                    sizeof(float));
            memcpy(&vertex.normal.z, normalBytes + i * normalStride + 2 * sizeof(float),
                    sizeof(float));
        }
        if (tangentBytes != nullptr) {
            memcpy(&vertex.tangent.x, tangentBytes + i * tangentStride + 0 * sizeof(float),
                    sizeof(float));
            memcpy(&vertex.tangent.y, tangentBytes + i * tangentStride + 1 * sizeof(float),
                    sizeof(float));
            memcpy(&vertex.tangent.z, tangentBytes + i * tangentStride + 2 * sizeof(float),
                    sizeof(float));
            memcpy(&vertex.tangent.w, tangentBytes + i * tangentStride + 3 * sizeof(float),
                    sizeof(float));
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

void MeshBuffer::loadMeshes(Scene const& scene) {
    std::unordered_map<std::string, bool> loaded;

    for (auto const& inst: scene.meshInstances) {
        if (loaded.contains(inst.gltfPath)) {
            continue;
        }
        loaded[inst.gltfPath] = true;

        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string warn;
        std::string err;

        bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, inst.gltfPath);
        if (!warn.empty()) {
            std::cerr << "glTF warning: " << warn << "\n";
        }
        if (!ret) {
            throw std::runtime_error("failed to load glTF model: " + err);
        }

        uint32_t firstIndex = static_cast<uint32_t>(indices.size());
        std::unordered_map<Vertex, uint32_t> uniqueVertices;
        for (auto const& mesh: model.meshes) {
            for (auto const& primitive: mesh.primitives) {
                loadPrimitive(model, primitive, uniqueVertices);
            }
        }

        bool doubleSided = false;
        if (!model.materials.empty()) {
            doubleSided = model.materials[0].doubleSided;
        }

        meshRanges[inst.gltfPath] = { firstIndex,
            static_cast<uint32_t>(indices.size()) - firstIndex, doubleSided };
        std::cout << "Model loaded (" << inst.gltfPath << "): " << vertices.size()
                  << " unique vertices, " << indices.size() << " indices\n";

        // Extract embedded textures from the first material if present.
        // Flip uvs vertically (1-y) to match vulkan's coordinate system
        if (!model.materials.empty()) {
            auto extractTex = [&](int texIndex,
                                      std::unordered_map<std::string, EmbeddedTexture>& map,
                                      std::string const& label) {
                if (texIndex < 0) {
                    return;
                }
                int imageIndex = model.textures[texIndex].source;
                auto const& img = model.images[imageIndex];
                if (img.image.empty()) {
                    return;
                }
                EmbeddedTexture tex;
                tex.width = static_cast<uint32_t>(img.width);
                tex.height = static_cast<uint32_t>(img.height);
                tex.pixels.resize(img.image.size());
                size_t rowBytes = tex.width * 4;
                for (uint32_t row = 0; row < tex.height; ++row) {
                    memcpy(tex.pixels.data() + row * rowBytes,
                            img.image.data() + (tex.height - 1 - row) * rowBytes, rowBytes);
                }
                map[inst.gltfPath] = std::move(tex);
                std::cout << label << " extracted from GLB (" << inst.gltfPath
                          << "): " << map[inst.gltfPath].width << "x" << map[inst.gltfPath].height
                          << "\n";
            };

            auto const& mat = model.materials[0];
            extractTex(mat.pbrMetallicRoughness.baseColorTexture.index, albedoMaps, "Albedo");
            extractTex(mat.normalTexture.index, normalMaps, "Normal map");
        }
    }
}

void MeshBuffer::uploadBuffers(VulkanContext const& ctx, CommandService const& cmds) {
    auto [uploadedVertices, vertexMemory] = uploadDeviceBuffer(ctx, cmds, vertices.data(),
            sizeof(Vertex) * vertices.size(), vk::BufferUsageFlagBits::eVertexBuffer);
    vertexBuffer = std::move(uploadedVertices);
    vertexBufferMemory = std::move(vertexMemory);

    auto [uploadedIndices, indexMemory] = uploadDeviceBuffer(ctx, cmds, indices.data(),
            sizeof(uint32_t) * indices.size(), vk::BufferUsageFlagBits::eIndexBuffer);
    indexBuffer = std::move(uploadedIndices);
    indexBufferMemory = std::move(indexMemory);
}
