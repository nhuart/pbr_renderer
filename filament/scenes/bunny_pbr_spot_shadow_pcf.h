#pragma once

#include "bunny_pbr_spot.h"

inline SceneDesc buildBunnyPbrSpotShadowPcfScene() {
    SceneDesc sceneDesc = buildBunnyPbrSpotScene();
    sceneDesc.title = "Stanford Bunny - PBR Spot Light PCF Shadow";
    sceneDesc.shadowType = filament::ShadowType::PCF;
    sceneDesc.lights.front().castShadows = true;
    sceneDesc.meshes.front().castShadows = true;
    for (auto& mesh: sceneDesc.meshes) {
        mesh.receiveShadows = true;
    }
    return sceneDesc;
}
