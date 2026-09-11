#pragma once

#include "renderer/material.hpp"
#include "renderer/mesh_buffer.hpp"

struct TextureAtlas;

struct RenderObject {
    uint32_t gameObjectIndex = 0;
    MeshIndexRange range;
    TextureAtlas const* texture = nullptr;
    Material const* material = nullptr;
    MaterialInstance materialInstance;
};
