#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "renderer/model_asset.hpp"
#include "renderer/vertex.hpp"
#include "scene/types.hpp"

struct VulkanContext;
struct CommandService;

struct MeshBuffer {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::unordered_map<std::string, ModelAsset> models;
    vk::raii::Buffer vertexBuffer = nullptr;
    vk::raii::DeviceMemory vertexBufferMemory = nullptr;
    vk::raii::Buffer indexBuffer = nullptr;
    vk::raii::DeviceMemory indexBufferMemory = nullptr;

    MeshBuffer(VulkanContext const& ctx, CommandService const& cmds, Scene const& scene);

private:
    void loadModel(std::string const& gltfPath);
    void uploadBuffers(VulkanContext const& ctx, CommandService const& cmds);
};
