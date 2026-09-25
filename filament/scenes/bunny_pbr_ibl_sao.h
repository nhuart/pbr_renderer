#pragma once

#include "bunny_transforms.h"
#include "scene.h"

#include <utils/Path.h>

inline SceneDesc buildBunnyPbrIblSaoScene() {
    SceneDesc sceneDesc;
    sceneDesc.title = "Stanford Bunny - PBR IBL SAO";
    sceneDesc.iblDir = "ibl/tree_lined_driveway_4k";
    sceneDesc.camera.eye = { 2.143304f, 1.750000f, 2.143304f };
    sceneDesc.sao = true;

    sceneDesc.meshes = {
        MeshEntry{
            .path = utils::Path("models/stanford_bunny.glb"),
            .transform = bunnyTransform(),
            .baseColor = { 0.8f, 0.7f, 0.6f, 1.0f },
            .metallic = 0.0f,
            .roughness = 0.5f,
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
