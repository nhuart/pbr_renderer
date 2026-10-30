#pragma once

#include <span>
#include <vector>

#include "renderer/vertex.hpp"

namespace gltf {

// Returns one tangent per index, preserving discontinuities at shared vertices.
// normalTexCoord must contain the original, unflipped glTF UVs.
std::vector<glm::vec4> generateTangents(std::span<Vertex const> vertices,
        std::span<uint32_t const> indices);

} // namespace gltf
