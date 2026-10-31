#pragma once

#include <unordered_map>

#include "renderer/material.hpp"
#include "renderer/model_asset.hpp"

struct TextureAtlas;

struct MaterialBinding {
    MaterialPipelineKey pipelineKey;
    TextureAtlas const* texture = nullptr;
    TextureAtlas const* normalMap = nullptr;
    Material const* pipeline = nullptr;
    MaterialInstance materialInstance;
};

struct RenderObject {
    uint32_t gameObjectIndex = 0;
    ModelAsset const* model = nullptr;
    std::unordered_map<uint32_t, MaterialBinding> materialBindings;
};
