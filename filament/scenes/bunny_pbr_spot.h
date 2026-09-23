#pragma once

#include "bunny_transforms.h"
#include "scene.h"

#include <filament/LightManager.h>
#include <utils/Path.h>

inline SceneDesc buildBunnyPbrSpotScene() {
    SceneDesc sceneDesc;
    sceneDesc.title = "Stanford Bunny - PBR Spot Light";
    sceneDesc.camera.eye = { 2.143304f, 1.750000f, 2.143304f };
    sceneDesc.ambientIntensity = 0.03f;
    sceneDesc.camera.aperture = 4.0f;

    sceneDesc.lights = {
        LightDesc{
            .type = filament::LightManager::Type::SPOT,
            .direction = { 0.0f, -1.0f, 0.0f },
            .position = { 0.03f, 3.0f, -0.28f },
            .color = { 1.0f, 1.0f, 1.0f },
            .intensity = 500000.0f,
            .range = 6.0f,
            .innerConeAngleDegrees = 20.0f,
            .outerConeAngleDegrees = 30.0f,
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
