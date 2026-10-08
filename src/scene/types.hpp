#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include "core/camera.hpp"

enum class ShaderFeatures : uint8_t {
    None = 0,
    Ibl = 1 << 0,
    NormalMap = 1 << 1,
    HardShadow = 1 << 2,
    PcfShadow = 1 << 3,
    Ao = 1 << 4,
};
inline ShaderFeatures operator|(ShaderFeatures a, ShaderFeatures b) {
    return static_cast<ShaderFeatures>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline ShaderFeatures operator&(ShaderFeatures a, ShaderFeatures b) {
    return static_cast<ShaderFeatures>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline bool hasFeature(ShaderFeatures features, ShaderFeatures bit) {
    return (features & bit) != ShaderFeatures::None;
}

struct MeshInstance {
    std::string gltfPath;
    std::string texturePath;
    std::string vertexShader;
    std::string fragmentShader;
    // Resolved at renderer init time — do not set in JSON
    std::string resolvedFragShader;
    ShaderFeatures shaderFeatures = ShaderFeatures::None;
    glm::vec4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    glm::vec3 position = { 0.0f, 0.0f, 0.0f };
    glm::vec3 rotation = { 0.0f, 0.0f, 0.0f };
    glm::vec3 scale = { 1.0f, 1.0f, 1.0f };
    float metallic = 0.0f;
    float roughness = 0.5f;
    bool useNormalMap = true;
    bool castShadows = false;
    bool receiveShadows = false;
};

struct ParticleSystem {
    uint32_t count = 8192;
};

struct AmbientLight {
    float intensity;
    float iblIntensity = 30000.0f; // matches Filament's IndirectLight::Builder::intensity()
};

enum class ShadowType { Hard, PCF };
enum class ToneMapping { Reinhard, None };

struct DirectionalLight {
    glm::vec3 direction;
    glm::vec3 color;
    float intensity = 1.0f; // illuminance in lux = lm/m2
    bool castShadow = false;
    ShadowType shadowType = ShadowType::Hard;
    float shadowBias = 0.005f;
};

struct SpotLight {
    glm::vec3 position;
    glm::vec3 direction;
    glm::vec3 color;
    float intensity = 1.0f; // luminous power in lm;
    float innerConeAngle;   // degrees
    float outerConeAngle;   // degrees
    float range;
};

struct PointLight {
    glm::vec3 position;
    glm::vec3 color;
    float intensity = 1.0f; // luminous power in lm;
    float range;
};

using Light = std::variant<AmbientLight, DirectionalLight, SpotLight, PointLight>;

struct SaoConfig {
    float radius = 1.0f;
    float bias = 0.004f;
    float power = 0.75f;
    float intensity = 1.0f;
    int sampleCount = 16;
    int spiralTurns = 7;
    int kernelRadius = 5;
    float depthThreshold = 0.001f;
};

struct GtaoConfig {
    float radius = 1.0f;
    float thicknessHeuristic = 0.004f;
    float power = 2.0f;
    float intensity = 1.0f;
    int stepCount = 3;
    int directionCount = 4;
    int kernelRadius = 11;
    float depthThreshold = 0.05f;
};

struct Scene {
    std::vector<MeshInstance> meshInstances;
    std::optional<ParticleSystem> particles;
    std::vector<Light> lights;
    Camera camera;
    ToneMapping toneMapping = ToneMapping::Reinhard;
    std::optional<std::string> iblPath;
    bool skybox = false;
    std::optional<SaoConfig> sao;
    std::optional<GtaoConfig> gtao;
};
