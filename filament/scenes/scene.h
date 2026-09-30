#pragma once

#include "../camera.h"
#include "../light.h"
#include "../mesh_loader.h"

#include <filament/Options.h>
#include <filament/View.h>

#include <optional>
#include <string>
#include <vector>

struct SceneDesc {
    std::string title;
    std::string iblDir;
    uint32_t width = 800;
    uint32_t height = 600;
    CameraParams camera;
    std::vector<LightDesc> lights;
    float ambientIntensity = 0.0f;
    filament::ShadowType shadowType = filament::ShadowType::PCF;
    std::optional<filament::View::AmbientOcclusionOptions> ambientOcclusion;
    bool disableMultiBounceAO = false;
    std::vector<MeshEntry> meshes;
    bool showSkybox = true;
};
