#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

// ---------------------------------------------------------------------------
// Scene description (loaded from JSON)
// ---------------------------------------------------------------------------

struct MeshInstance {
    std::string gltfPath;
    std::string texturePath;
    glm::vec3 position = { 0.0f, 0.0f, 0.0f };
    glm::vec3 rotation = { 0.0f, 0.0f, 0.0f }; // degrees
    glm::vec3 scale = { 1.0f, 1.0f, 1.0f };
};

struct ParticleSystem {
    uint32_t count = 8192;
};

struct Scene {
    std::vector<MeshInstance> meshInstances;
    std::optional<ParticleSystem> particles;
};

// ---------------------------------------------------------------------------
// Vertex
// ---------------------------------------------------------------------------

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;

    bool operator==(Vertex const& other) const {
        return pos == other.pos && color == other.color && texCoord == other.texCoord;
    }

    static vk::VertexInputBindingDescription getBindingDescription() {
        return { .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = vk::VertexInputRate::eVertex };
    }

    static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions() {
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
                .offset = offsetof(Vertex, texCoord) } } };
    }
};

namespace std {
template<>
struct hash<Vertex> {
    size_t operator()(Vertex const& vtx) const {
        return ((hash<glm::vec3>()(vtx.pos) ^ (hash<glm::vec3>()(vtx.color) << 1)) >> 1) ^
               (hash<glm::vec2>()(vtx.texCoord) << 1);
    }
};
} // namespace std

// ---------------------------------------------------------------------------
// GPU data structs
// ---------------------------------------------------------------------------

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

struct ComputeUBO {
    float deltaTime;
};

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

// ---------------------------------------------------------------------------
// GameObject
// ---------------------------------------------------------------------------

struct GameObject {
    glm::vec3 position = { 0.0f, 0.0f, 0.0f };
    glm::vec3 rotation = { 0.0f, 0.0f, 0.0f };
    glm::vec3 scale = { 1.0f, 1.0f, 1.0f };

    [[nodiscard]] glm::mat4 getModelMatrix() const {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        model = glm::rotate(model, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
        return glm::scale(model, scale);
    }
};
