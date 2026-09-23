#pragma once

#include "bunny_transforms.h"
#include "scene.h"

#include <filament/LightManager.h>
#include <utils/Path.h>

inline SceneDesc buildBunnyPbrDirectionalScene() {
    SceneDesc sceneDesc;
    sceneDesc.title = "Stanford Bunny - PBR Directional";
    sceneDesc.camera.eye = { 2.143304f, 1.750000f, 2.143304f };
    sceneDesc.ambientIntensity = 0.03f;
    sceneDesc.camera.aperture = 4.0f;

    sceneDesc.lights = {
        LightDesc{
            .type = filament::LightManager::Type::DIRECTIONAL,
            .direction = { 1.0f, 2.0f, 1.0f },
            .color = { 1.0f, 1.0f, 1.0f },
            .intensity = 10000.0f,
        },
    };

    sceneDesc.meshes = {
        MeshEntry{
            .path = utils::Path("models/stanford_bunny.glb"),
            .transform = bunnyTransform(),
            .baseColor = { 0.8f, 0.7f, 0.6f, 1.0f },
            .metallic = 0.0f,
            .roughness = 0.4f,
        },
        MeshEntry{
            .path = utils::Path("models/plane.glb"),
            .transform = planeTransform(),
            .baseColor = { 0.6f, 0.6f, 0.6f, 1.0f },
            .metallic = 0.0f,
            .roughness = 0.8f,
        },
    };

    return sceneDesc;
}
