#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "renderer/gltf_transform.hpp"

#include "core/command_service.hpp"
#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "renderer/gltf_primitive.hpp"
#include "renderer/mesh_buffer.hpp"

#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {

tinygltf::Model loadGltfModel(std::string const& gltfPath) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string warning;
    std::string error;
    bool loaded = loader.LoadBinaryFromFile(&model, &error, &warning, gltfPath);
    if (!warning.empty()) {
        std::cerr << "glTF warning: " << warning << '\n';
    }
    if (!loaded) {
        throw std::runtime_error("failed to load glTF model (" + gltfPath + "): " + error);
    }
    return model;
}

std::optional<EmbeddedTexture> extractEmbeddedTexture(tinygltf::Model const& model,
        int textureIndex) {
    if (textureIndex < 0) {
        return std::nullopt;
    }
    auto const& image = model.images[model.textures[textureIndex].source];
    if (image.image.empty()) {
        return std::nullopt;
    }
    EmbeddedTexture texture;
    texture.width = static_cast<uint32_t>(image.width);
    texture.height = static_cast<uint32_t>(image.height);
    texture.pixels.resize(image.image.size());
    // Match the vertical UV flip applied when reading vertices.
    size_t rowBytes = texture.width * 4;
    for (uint32_t row = 0; row < texture.height; ++row) {
        memcpy(texture.pixels.data() + row * rowBytes,
                image.image.data() + (texture.height - 1 - row) * rowBytes, rowBytes);
    }
    return texture;
}

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

void MeshBuffer::loadPrimitive(tinygltf::Model const& model, tinygltf::Primitive const& primitive,
        glm::mat4 const& transform, std::unordered_map<Vertex, uint32_t>& uniqueVertices) {
    gltf::PrimitiveGeometry geometry(model, primitive);
    gltf::VertexTransform vertexTransform(transform);
    size_t firstIndex = indices.size();
    for (size_t corner = 0; corner < geometry.indexCount(); ++corner) {
        Vertex vertex = geometry.vertexAtCorner(corner);
        vertexTransform.apply(vertex);
        indices.push_back(appendVertex(vertex, uniqueVertices));
    }
    if (vertexTransform.reversesWinding()) {
        for (size_t i = firstIndex; i + 2 < indices.size(); i += 3) {
            std::swap(indices[i + 1], indices[i + 2]);
        }
    }
}

uint32_t MeshBuffer::appendVertex(Vertex const& vertex,
        std::unordered_map<Vertex, uint32_t>& uniqueVertices) {
    auto [entry, inserted] =
            uniqueVertices.insert({ vertex, static_cast<uint32_t>(vertices.size()) });
    if (inserted) {
        vertices.push_back(vertex);
    }
    return entry->second;
}

void MeshBuffer::loadMeshes(Scene const& scene) {
    for (auto const& instance: scene.meshInstances) {
        if (!meshRanges.contains(instance.gltfPath)) {
            loadModel(instance.gltfPath);
        }
    }
}

void MeshBuffer::loadModel(std::string const& gltfPath) {
    auto model = loadGltfModel(gltfPath);
    uint32_t firstIndex = static_cast<uint32_t>(indices.size());
    std::unordered_map<Vertex, uint32_t> uniqueVertices;
    for (auto const& meshInstance: gltf::meshInstances(model)) {
        auto const& mesh = model.meshes.at(meshInstance.mesh);
        for (auto const& primitive: mesh.primitives) {
            loadPrimitive(model, primitive, meshInstance.transform, uniqueVertices);
        }
    }
    bool doubleSided = !model.materials.empty() && model.materials.front().doubleSided;
    meshRanges[gltfPath] = calculateMeshRange(firstIndex, doubleSided);
    std::cout << "Model loaded (" << gltfPath << "): " << vertices.size() << " unique vertices, "
              << indices.size() << " indices\n";
    loadEmbeddedTextures(model, gltfPath);
}

MeshIndexRange MeshBuffer::calculateMeshRange(uint32_t firstIndex, bool doubleSided) const {
    glm::vec3 boundsMin(std::numeric_limits<float>::max());
    glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
    for (size_t i = firstIndex; i < indices.size(); ++i) {
        auto const& position = vertices[indices[i]].pos;
        boundsMin = glm::min(boundsMin, position);
        boundsMax = glm::max(boundsMax, position);
    }
    return { firstIndex, static_cast<uint32_t>(indices.size()) - firstIndex, doubleSided, boundsMin,
        boundsMax };
}

void MeshBuffer::loadEmbeddedTextures(tinygltf::Model const& model, std::string const& gltfPath) {
    if (model.materials.empty()) {
        return;
    }
    auto storeTexture = [&](int textureIndex,
                                std::unordered_map<std::string, EmbeddedTexture>& textures,
                                char const* label) {
        if (auto texture = extractEmbeddedTexture(model, textureIndex)) {
            std::cout << label << " extracted from GLB (" << gltfPath << "): " << texture->width
                      << "x" << texture->height << '\n';
            textures[gltfPath] = std::move(*texture);
        }
    };
    auto const& material = model.materials.front();
    storeTexture(material.pbrMetallicRoughness.baseColorTexture.index, albedoMaps, "Albedo");
    storeTexture(material.normalTexture.index, normalMaps, "Normal map");
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
