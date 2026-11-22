#pragma once

#include "renderer/model_asset.hpp"

namespace tinygltf {
struct Material;
}
namespace gltf {
MaterialDefinition readMaterial(tinygltf::Material const& material);
}
