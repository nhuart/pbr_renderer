#pragma once

#include <cstring>
#include <unordered_map>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "scene/types.hpp"

namespace tinygltf {
struct Model;
struct Primitive;
struct Accessor;
}  // namespace tinygltf

struct VulkanContext;
struct CommandService;

struct MeshBuffer {
    std::vector<Vertex>    vertices;
    std::vector<uint32_t>  indices;
    vk::raii::Buffer       vertexBuffer       = nullptr;
    vk::raii::DeviceMemory vertexBufferMemory = nullptr;
    vk::raii::Buffer       indexBuffer        = nullptr;
    vk::raii::DeviceMemory indexBufferMemory  = nullptr;

    MeshBuffer(VulkanContext const& ctx, CommandService const& cmds, Scene const& scene);

private:
    void loadMeshes(Scene const& scene);
    void loadPrimitive(tinygltf::Model const& model, tinygltf::Primitive const& primitive,
            std::unordered_map<Vertex, uint32_t>& uniqueVertices);
    void uploadBuffers(VulkanContext const& ctx, CommandService const& cmds);
    static uint8_t const* accessorData(tinygltf::Model const& model,
            tinygltf::Accessor const& acc);
    template <typename T>
    static T readAt(uint8_t const* ptr, size_t index) {
        T val{};
        memcpy(&val, ptr + index * sizeof(T), sizeof(T));
        return val;
    }
};
