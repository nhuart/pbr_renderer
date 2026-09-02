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
        if (it != mTextures.end()) return *it->second;
        auto [inserted, ok] =
                mTextures.emplace(path, std::make_unique<TextureAtlas>(ctx, cmds, path));
        return *inserted->second;
    }

private:
    std::unordered_map<std::string, std::unique_ptr<TextureAtlas>> mTextures;
};
