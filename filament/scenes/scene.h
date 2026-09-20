#pragma once

#include "../camera.h"
#include "../mesh_loader.h"

#include <string>
#include <vector>

struct SceneDesc {
    std::string title;
    std::string iblDir;
    uint32_t width = 800;
    uint32_t height = 600;
    CameraParams camera;
    std::vector<MeshEntry> meshes;
};
