#pragma once

#include "scene.h"

#include <math/mat4.h>
#include <math/vec3.h>
#include <utils/Path.h>

#include <string>

using filament::math::float3;
using filament::math::mat4f;

// Filament uses Y-up coordinates; pbr_renderer uses Z-up.
inline mat4f bunnyTransform() {
    mat4f translation = mat4f::translation(float3{ 0.03f, -1.19f, -0.28f });
    mat4f rotationY = mat4f::rotation(45.0f * float(M_PI) / 180.0f, float3{ 0, 1, 0 });
    mat4f scale = mat4f::scaling(float3{ 10.0f });
    return translation * rotationY * scale;
}

inline mat4f planeTransform() {
    mat4f translation = mat4f::translation(float3{ 0.03f, -0.857f, -0.28f });
    mat4f scale = mat4f::scaling(float3{ 3.0f });
    return translation * scale;
}

inline SceneDesc buildBunnyIblScene() {
    SceneDesc sceneDesc;
    sceneDesc.title = "Stanford Bunny — Filament IBL";
    sceneDesc.iblDir = "ibl/tree_lined_driveway_4k";
    sceneDesc.camera.eye = { 2.143304f, 1.750000f, 2.143304f };

    const std::string bunnyPath = "models/stanford_bunny.glb";
    const std::string planePath = "models/plane.glb";

    sceneDesc.meshes = {
        MeshEntry{
            .path = utils::Path(bunnyPath),
            .transform = bunnyTransform(),
            .baseColor = { 0.8f, 0.7f, 0.6f, 1.0f },
            .metallic = 1.0f,
            .roughness = 0.0f,
        },
        MeshEntry{
            .path = utils::Path(planePath),
            .transform = planeTransform(),
            .baseColor = { 0.6f, 0.6f, 0.6f, 1.0f },
            .metallic = 0.0f,
            .roughness = 0.8f,
        },
    };

    return sceneDesc;
}
