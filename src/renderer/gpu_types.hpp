#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "scene/types.hpp"

constexpr uint32_t MAX_LIGHTS = 8;

// Packed GPU representation of one light.
// std140 requires 16-byte alignment per field, so data is packed into vec4s.
struct GpuLight {
    alignas(16) glm::vec4 colorAndType;        // xyz=RGB intensity, w=lightType (1/2/3)
    alignas(16) glm::vec4 positionAndInvRange; // xyz=world position, w=1/range (spot/point)
    alignas(16) glm::vec4 direction;       // xyz=normalized direction (directional/spot), w=unused
    alignas(16) glm::vec4 coneScaleOffset; // x=scale, y=offset for cone attenuation (spot only)

    static GpuLight from(AmbientLight const&, float) { return {}; }

    static GpuLight from(DirectionalLight const& light, float exposure) {
        GpuLight gpuLight{};
        gpuLight.colorAndType = glm::vec4(light.color * light.intensity * exposure, 1.0f);
        gpuLight.direction = glm::vec4(glm::normalize(light.direction), 0.0f);
        return gpuLight;
    }

    static GpuLight from(SpotLight const& light, float exposure) {
        float cosOuter = glm::cos(glm::radians(light.outerConeAngle));
        float cosInner = glm::cos(glm::radians(light.innerConeAngle));
        constexpr float invPi = 1.0f / 3.14159265358979f;
        float luminousIntensity = light.intensity * invPi; // matches Filament Type::SPOT: lm/π
        float coneScale = 1.0f / glm::max(cosInner - cosOuter, 1e-4f);
        GpuLight gpuLight{};
        gpuLight.colorAndType = glm::vec4(light.color * luminousIntensity * exposure, 2.0f);
        gpuLight.positionAndInvRange = glm::vec4(light.position, inverseRange(light.range));
        gpuLight.direction = glm::vec4(glm::normalize(light.direction), 0.0f);
        gpuLight.coneScaleOffset = glm::vec4(coneScale, -cosOuter * coneScale, 0.0f, 0.0f);
        return gpuLight;
    }

    static GpuLight from(PointLight const& light, float exposure) {
        constexpr float invFourPi = 1.0f / (4.0f * 3.14159265358979f);
        GpuLight gpuLight{};
        gpuLight.colorAndType =
                glm::vec4(light.color * light.intensity * invFourPi * exposure, 3.0f);
        gpuLight.positionAndInvRange = glm::vec4(light.position, inverseRange(light.range));
        return gpuLight;
    }

private:
    static float inverseRange(float range) { return 1.0f / glm::max(range, 1e-4f); }
};

struct LightUBO {
    alignas(16) glm::uvec4 counts; // x = active light count (uvec4 for std140 padding)
    GpuLight lights[MAX_LIGHTS];
};

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::mat4 normalMatrix;
    alignas(16) glm::vec4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    alignas(16) glm::vec4 cameraPos = { 0.0f, 0.0f, 0.0f, 0.0f }; // xyz = world position
    alignas(16) glm::vec4 pbrParams = { 0.0f, 0.5f, 0.0f, 0.0f }; // x=metallic, y=roughness
};

struct IblSHUBO {
    alignas(16) glm::vec4 sh[9]; // L0..L2 SH coefficients (RGB in xyz, w unused)
};

struct ShadowUBO {
    alignas(16) glm::mat4 lightSpaceTransform; // VP*M for vertex pass, VP for fragment pass
    alignas(16) float shadowBias = 0.005f;
};

struct ComputeUBO {
    float deltaTime;
};
