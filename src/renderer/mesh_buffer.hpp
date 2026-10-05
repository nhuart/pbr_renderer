#pragma once

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "renderer/vertex.hpp"
#include "scene/types.hpp"

namespace tinygltf {
struct Model;
struct Primitive;
struct Accessor;
} // namespace tinygltf

struct VulkanContext;
struct CommandService;

struct MeshIndexRange {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    bool doubleSided = false;
};

struct EmbeddedTexture {
    std::vector<uint8_t> pixels; // RGBA8 decoded pixels
    uint32_t width = 0;
    uint32_t height = 0;
};

struct MeshBuffer {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::unordered_map<std::string, MeshIndexRange> meshRanges;  // keyed by gltfPath
    std::unordered_map<std::string, EmbeddedTexture> albedoMaps; // keyed by gltfPath
    std::unordered_map<std::string, EmbeddedTexture> normalMaps; // keyed by gltfPath
    vk::raii::Buffer vertexBuffer = nullptr;
    vk::raii::DeviceMemory vertexBufferMemory = nullptr;
    vk::raii::Buffer indexBuffer = nullptr;
    vk::raii::DeviceMemory indexBufferMemory = nullptr;

    MeshBuffer(VulkanContext const& ctx, CommandService const& cmds, Scene const& scene);

private:
    void loadMeshes(Scene const& scene);
    void loadPrimitive(tinygltf::Model const& model, tinygltf::Primitive const& primitive,
            std::unordered_map<Vertex, uint32_t>& uniqueVertices);
    void uploadBuffers(VulkanContext const& ctx, CommandService const& cmds);
    static uint8_t const* accessorData(tinygltf::Model const& model, tinygltf::Accessor const& acc);
    template<typename T>
    static T readAt(uint8_t const* ptr, size_t index) {
        T val{};
        memcpy(&val, ptr + index * sizeof(T), sizeof(T));
        return val;
    }
};
