#pragma once

#include <filament/Engine.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filamentapp/AssetLoader.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/TextureProvider.h>
#include <math/mat4.h>
#include <math/vec4.h>
#include <utils/Path.h>

using filament::math::float4;
using filament::math::mat4f;

struct MeshEntry {
    utils::Path path;
    mat4f transform;
    float4 baseColor = { 1, 1, 1, 1 };
    float metallic = 0.0f;
    float roughness = 1.0f;
    bool castShadows = false;
    bool receiveShadows = false;
    bool useNormalMap = true;

    filament::gltfio::FilamentAsset* asset = nullptr;
    filament::gltfio::ResourceLoader* resourceLoader = nullptr;
    filament::gltfio::TextureProvider* textureProvider = nullptr;
    bool loaded = false;
};

struct MeshLoaderContext {
    filament::Engine* engine;
    filament::app::AssetLoader* appLoader;
    filament::gltfio::AssetLoader* gltfLoader;
};

void loadMesh(MeshLoaderContext const& loaderContext, MeshEntry& entry);
void applyMaterial(filament::Engine& engine, MeshEntry const& entry);
void addMeshToScene(filament::Scene& scene, MeshEntry& entry);
void destroyMesh(filament::gltfio::AssetLoader& gltfLoader, MeshEntry& entry);
