#include "renderer/mesh_buffer.hpp"

#include "core/command_service.hpp"
#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "renderer/gltf_model.hpp"

#include <cstring>
#include <iostream>
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
    for (auto const& instance: scene.meshInstances) {
        if (!models.contains(instance.gltfPath)) {
            loadModel(instance.gltfPath);
        }
    }
    uploadBuffers(ctx, cmds);
}

void MeshBuffer::loadModel(std::string const& path) {
    auto imported = gltf::loadModel(path);
    uint32_t firstVertex = static_cast<uint32_t>(vertices.size());
    uint32_t firstIndex = static_cast<uint32_t>(indices.size());
    vertices.insert(vertices.end(), imported.vertices.begin(), imported.vertices.end());
    for (uint32_t index: imported.indices) {
        indices.push_back(firstVertex + index);
    }
    imported.asset.range.firstIndex += firstIndex;
    for (auto& primitive: imported.asset.primitives) {
        primitive.range.firstIndex += firstIndex;
    }
    std::cout << "Model loaded (" << path << "): " << imported.vertices.size() << " vertices, "
              << imported.asset.primitives.size() << " primitives\n";
    models.emplace(path, std::move(imported.asset));
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
