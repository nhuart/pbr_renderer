#pragma once

#include <cstdint>
#include <span>

namespace tinygltf {
struct BufferView;
struct Model;
} // namespace tinygltf

namespace gltf {

std::span<uint8_t const> bufferViewData(tinygltf::Model const& model,
        tinygltf::BufferView const& view);
int appendBufferView(tinygltf::Model& model, std::span<uint8_t const> bytes, int target = 0);
void compactGeometryBuffers(tinygltf::Model& model);

} // namespace gltf
