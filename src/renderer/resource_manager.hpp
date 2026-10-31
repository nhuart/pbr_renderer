#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "renderer/model_asset.hpp"
#include "renderer/texture_atlas.hpp"

struct VulkanContext;
struct CommandService;

struct ResourceManager {
    TextureAtlas const& getTexture(VulkanContext const& ctx, CommandService const& cmds,
            std::string const& path) {
        if (auto it = mTextures.find(path); it != mTextures.end()) {
            return *it->second;
        }
        return cacheTexture(path, std::make_unique<TextureAtlas>(ctx, cmds, path));
    }

    TextureAtlas const& getEmbeddedTexture(VulkanContext const& ctx, CommandService const& cmds,
            std::string const& assetPath, ModelAsset const& model, TextureReference reference,
            TextureUsage usage) {
        if (!reference.hasTexture()) {
            return getFallback(ctx, cmds, usage);
        }
        auto key = assetPath + "#texture:" + std::to_string(reference.texture) +
                   (usage == TextureUsage::NormalMap ? ":linear" : ":srgb");
        if (auto it = mTextures.find(key); it != mTextures.end()) {
            return *it->second;
        }
        auto const& definition = model.textures.at(reference.texture);
        auto const& image = model.images.at(definition.image);
        return cacheTexture(key, std::make_unique<TextureAtlas>(ctx, cmds, image.pixels.data(),
                                         image.width, image.height, usage, definition.sampler));
    }

private:
    TextureAtlas const& getFallback(VulkanContext const& ctx, CommandService const& cmds,
            TextureUsage usage) {
        std::string key = usage == TextureUsage::NormalMap ? "#flat-normal" : "#white";
        if (auto it = mTextures.find(key); it != mTextures.end()) {
            return *it->second;
        }
        if (usage == TextureUsage::NormalMap) {
            static constexpr uint8_t flatNormal[] = { 128, 128, 255, 255 };
            return cacheTexture(key,
                    std::make_unique<TextureAtlas>(ctx, cmds, flatNormal, 1, 1, usage));
        }
        return cacheTexture(key, std::make_unique<TextureAtlas>(ctx, cmds, 255, 255, 255));
    }

    TextureAtlas const& cacheTexture(std::string const& key,
            std::unique_ptr<TextureAtlas> texture) {
        auto entry = mTextures.emplace(key, std::move(texture));
        return *entry.first->second;
    }

    std::unordered_map<std::string, std::unique_ptr<TextureAtlas>> mTextures;
};
