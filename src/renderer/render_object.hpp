#pragma once

#include "renderer/material.hpp"

struct TextureAtlas;

struct RenderObject {
    uint32_t gameObjectIndex = 0;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    bool doubleSided = false;
    TextureAtlas const* texture = nullptr;
    Material const* material = nullptr;
    MaterialInstance materialInstance;
};
