#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "renderer/texture_atlas.hpp"

struct VulkanContext;
struct CommandService;

struct ResourceManager {
    TextureAtlas const& getTexture(VulkanContext const& ctx, CommandService const& cmds,
            std::string const& path) {
        auto it = mTextures.find(path);
        if (it != mTextures.end()) {
            return *it->second;
        }
        std::unique_ptr<TextureAtlas> tex;
        if (path.empty()) {
            tex = std::make_unique<TextureAtlas>(ctx, cmds, 255, 255, 255);
        } else {
            tex = std::make_unique<TextureAtlas>(ctx, cmds, path);
        }
        auto [inserted, ok] = mTextures.emplace(path, std::move(tex));
        return *inserted->second;
    }

private:
    std::unordered_map<std::string, std::unique_ptr<TextureAtlas>> mTextures;
};
