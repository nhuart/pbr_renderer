#pragma once

namespace tinygltf {
struct Model;
}

namespace gltf {

// Stores generated tangents in the source geometry, splitting vertices at tangent seams.
bool bakeMissingTangents(tinygltf::Model& model);

} // namespace gltf
