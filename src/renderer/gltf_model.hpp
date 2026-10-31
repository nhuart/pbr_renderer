#pragma once

#include <string>

#include "renderer/model_asset.hpp"
#include "renderer/vertex.hpp"

namespace tinygltf {
struct Model;
}

namespace gltf {

struct ImportedModel {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    ModelAsset asset;
};

ImportedModel importModel(tinygltf::Model const& model);
ImportedModel loadModel(std::string const& path);

} // namespace gltf
