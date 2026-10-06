#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <vulkan/vulkan_raii.hpp>

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 normal;
    glm::vec4 tangent; // xyz = tangent direction, w = handedness (+1 or -1)

    bool operator==(Vertex const& other) const {
        return pos == other.pos && color == other.color && texCoord == other.texCoord &&
               normal == other.normal && tangent == other.tangent;
    }

    static vk::VertexInputBindingDescription getBindingDescription() {
        return { .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = vk::VertexInputRate::eVertex };
    }

    static std::array<vk::VertexInputAttributeDescription, 5> getAttributeDescriptions() {
        return { { { .location = 0,
                       .binding = 0,
                       .format = vk::Format::eR32G32B32Sfloat,
                       .offset = offsetof(Vertex, pos) },
            { .location = 1,
                .binding = 0,
                .format = vk::Format::eR32G32B32Sfloat,
                .offset = offsetof(Vertex, color) },
            { .location = 2,
                .binding = 0,
                .format = vk::Format::eR32G32Sfloat,
                .offset = offsetof(Vertex, texCoord) },
            { .location = 3,
                .binding = 0,
                .format = vk::Format::eR32G32B32Sfloat,
                .offset = offsetof(Vertex, normal) },
            { .location = 4,
                .binding = 0,
                .format = vk::Format::eR32G32B32A32Sfloat,
                .offset = offsetof(Vertex, tangent) } } };
    }
};

namespace std {
template<>
struct hash<Vertex> {
    size_t operator()(Vertex const& vertex) const {
        size_t hashValue = hash<glm::vec3>()(vertex.pos);
        hashValue = (hashValue ^ (hash<glm::vec3>()(vertex.color) << 1)) >> 1;
        hashValue = (hashValue ^ (hash<glm::vec2>()(vertex.texCoord) << 1)) >> 1;
        hashValue = (hashValue ^ (hash<glm::vec3>()(vertex.normal) << 1)) >> 1;
        return hashValue;
    }
};
} // namespace std

struct Particle {
    glm::vec2 position;
    glm::vec2 velocity;
    glm::vec4 color;

    static vk::VertexInputBindingDescription getBindingDescription() {
        return { .binding = 0,
            .stride = sizeof(Particle),
            .inputRate = vk::VertexInputRate::eVertex };
    }

    static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions() {
        return { { { .location = 0,
                       .binding = 0,
                       .format = vk::Format::eR32G32Sfloat,
                       .offset = offsetof(Particle, position) },
            { .location = 1,
                .binding = 0,
                .format = vk::Format::eR32G32B32A32Sfloat,
                .offset = offsetof(Particle, color) } } };
    }
};
