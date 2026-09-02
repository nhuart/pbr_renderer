#pragma once

#include "renderer/material.hpp"

struct TextureAtlas;

struct RenderObject {
    uint32_t gameObjectIndex = 0;
    TextureAtlas const* texture = nullptr;
    MaterialInstance materialInstance;
};
