#pragma once

#include <string>

namespace tinygltf {
struct Model;
}

namespace gltf {

tinygltf::Model readBinaryModel(std::string const& path);
void saveGeneratedTangents(tinygltf::Model& model, std::string const& path);

} // namespace gltf
