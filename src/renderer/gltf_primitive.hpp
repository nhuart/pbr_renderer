#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "renderer/vertex.hpp"

namespace tinygltf {
struct Model;
struct Primitive;
} // namespace tinygltf

namespace gltf {

class PrimitiveGeometry {
public:
    PrimitiveGeometry(tinygltf::Model const& model, tinygltf::Primitive const& primitive);

    size_t indexCount() const { return mIndices.size(); }
    Vertex vertexAtCorner(size_t corner) const;
    void indexGeneratedTangents();
    std::span<Vertex const> vertices() const { return mVertices; }
    std::span<uint32_t const> indices() const { return mIndices; }
    std::span<glm::vec4 const> generatedTangents() const { return mCornerTangents; }

private:
    void generateMissingTangents(tinygltf::Model const& model,
            tinygltf::Primitive const& primitive);
    void flipTextureCoordinates();

    std::vector<Vertex> mVertices;
    std::vector<uint32_t> mIndices;
    std::vector<glm::vec4> mCornerTangents;
};

} // namespace gltf
