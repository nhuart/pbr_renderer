#pragma once

#include <span>
#include <vector>

#include "renderer/vertex.hpp"

namespace gltf {

struct TangentVertexRemap {
    std::vector<uint32_t> sources;
    std::vector<uint32_t> indices;
    std::vector<glm::vec4> tangents;
};

// Retains source indices wherever possible, splitting only conflicting corner tangents.
TangentVertexRemap splitTangentSeams(size_t vertexCount, std::span<uint32_t const> indices,
        std::span<glm::vec4 const> cornerTangents);

// Returns one tangent per index, preserving discontinuities at shared vertices.
// normalTexCoord must contain the original, unflipped glTF UVs.
std::vector<glm::vec4> generateTangents(std::span<Vertex const> vertices,
        std::span<uint32_t const> indices);

} // namespace gltf
