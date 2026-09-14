#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "renderer/mesh_buffer.hpp"
#include "renderer/texture_atlas.hpp"

struct VulkanContext;
struct CommandService;

struct ResourceManager {
    // Load albedo: prefers explicit KTX2 path, falls back to GLB-embedded albedo, then white.
    TextureAtlas const& getTexture(VulkanContext const& ctx, CommandService const& cmds,
            std::string const& path, std::string const& gltfPath = {},
            MeshBuffer const* meshBuffer = nullptr) {
        auto key = path.empty() ? (gltfPath + "#albedo") : path;
        auto it = mTextures.find(key);
        if (it != mTextures.end()) {
            return *it->second;
        }
        std::unique_ptr<TextureAtlas> tex;
        if (!path.empty()) {
            tex = std::make_unique<TextureAtlas>(ctx, cmds, path);
        } else if (meshBuffer) {
            auto albedoIt = meshBuffer->albedoMaps.find(gltfPath);
            if (albedoIt != meshBuffer->albedoMaps.end()) {
                auto const& albedo = albedoIt->second;
                tex = std::make_unique<TextureAtlas>(ctx, cmds, albedo.pixels.data(), albedo.width,
                        albedo.height, /*linear=*/false);
            } else {
                tex = std::make_unique<TextureAtlas>(ctx, cmds, 255, 255, 255);
            }
        } else {
            tex = std::make_unique<TextureAtlas>(ctx, cmds, 255, 255, 255);
        }
        auto [inserted, ok] = mTextures.emplace(key, std::move(tex));
        return *inserted->second;
    }

    // Returns a 1x1 flat normal (128,128,255) when no normal map data is available.
    TextureAtlas const& getNormalMap(VulkanContext const& ctx, CommandService const& cmds,
            std::string const& gltfPath, MeshBuffer const& meshBuffer) {
        auto key = gltfPath + "#normalmap";
        auto it = mTextures.find(key);
        if (it != mTextures.end()) {
            return *it->second;
        }
        std::unique_ptr<TextureAtlas> tex;
        auto normalMapIt = meshBuffer.normalMaps.find(gltfPath);
        if (normalMapIt != meshBuffer.normalMaps.end()) {
            auto const& normalMap = normalMapIt->second;
            tex = std::make_unique<TextureAtlas>(ctx, cmds, normalMap.pixels.data(),
                    normalMap.width, normalMap.height,
                    /*linear=*/true);
        } else {
            // Flat normal map: (128, 128, 255, 255) → (0,0,1) in tangent space
            tex = std::make_unique<TextureAtlas>(ctx, cmds, 128, 128, 255);
        }
        auto [inserted, ok] = mTextures.emplace(key, std::move(tex));
        return *inserted->second;
    }

private:
    std::unordered_map<std::string, std::unique_ptr<TextureAtlas>> mTextures;
};
