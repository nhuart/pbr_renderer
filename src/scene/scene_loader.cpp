#include "scene/scene_loader.hpp"

#include <cmath>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <string_view>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

glm::vec3 vec3FromJson(json const& j) {
    return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
}

glm::dvec3 dvec3FromJson(json const& j) {
    return { j[0].get<double>(), j[1].get<double>(), j[2].get<double>() };
}

void validateAo(Scene const& scene, std::string const& path) {
    if (scene.sao && scene.gtao) {
        throw std::runtime_error("scene cannot enable both sao and gtao: " + path);
    }
    if (!scene.gtao) {
        return;
    }

    auto const& gtao = *scene.gtao;
    bool invalidRadius = gtao.radius <= 0.0f;
    bool invalidThickness = gtao.thicknessHeuristic < 0.0f || gtao.thicknessHeuristic > 1.0f;
    bool invalidSampling = gtao.stepCount < 1 || gtao.directionCount < 1;
    bool invalidResponse = gtao.power <= 0.0f || gtao.intensity < 0.0f;
    bool invalidBlur = gtao.kernelRadius < 0 || gtao.depthThreshold <= 0.0f;
    if (invalidRadius || invalidThickness || invalidSampling || invalidResponse || invalidBlur) {
        throw std::runtime_error("invalid gtao settings: " + path);
    }
}

void validateCamera(Camera const& camera, std::string const& path) {
    if (camera.radius <= 0.0) {
        throw std::runtime_error("camera.radius must be > 0: " + path);
    }
    if (camera.fovDegrees <= 0.0 || camera.fovDegrees >= 180.0) {
        throw std::runtime_error("camera.fov must be in (0, 180): " + path);
    }
    if (camera.nearPlane <= 0.0) {
        throw std::runtime_error("camera.near must be > 0: " + path);
    }
    if (camera.farPlane <= camera.nearPlane) {
        throw std::runtime_error("camera.far must be > camera.near: " + path);
    }
    if (camera.elevation <= -90.0 || camera.elevation >= 90.0) {
        throw std::runtime_error("camera.elevation must be in (-90, 90): " + path);
    }
}

void validateSpotLightShadow(SpotLight const& light, std::string const& path) {
    if (!light.shadows.castShadow) {
        return;
    }
    bool invalidCone = !std::isfinite(light.outerConeAngle) || light.outerConeAngle <= 0.0f ||
                       light.outerConeAngle >= 90.0f;
    bool invalidRange = !std::isfinite(light.range) || light.range <= SPOT_SHADOW_NEAR_DISTANCE;
    float directionLength = glm::length(light.direction);
    bool invalidDirection = !std::isfinite(directionLength) || directionLength == 0.0f;
    if (invalidCone || invalidRange || invalidDirection) {
        throw std::runtime_error(
                "invalid shadow-casting spot light cone, range or direction: " + path);
    }
}

void validateLights(Scene const& scene, std::string const& path) {
    int shadowCastingLightCount = 0;
    for (auto const& light: scene.lights) {
        if (auto const* spot = std::get_if<SpotLight>(&light)) {
            validateSpotLightShadow(*spot, path);
        }
        auto const* shadows = shadowSettings(light);
        if (!shadows || !shadows->castShadow) {
            continue;
        }
        ++shadowCastingLightCount;
    }
    if (shadowCastingLightCount > 1) {
        throw std::runtime_error(
                "scene has " + std::to_string(shadowCastingLightCount) +
                " shadow-casting lights — only one directional or spot light is supported: " +
                path);
    }
    if (scene.volumetric && shadowCastingLightCount != 1) {
        throw std::runtime_error(
                "volumetric requires a shadow-casting directional or spot light: " + path);
    }
}

void validateMeshes(Scene const& scene, std::string const& path) {
    for (size_t i = 0; i < scene.meshInstances.size(); ++i) {
        auto const& instance = scene.meshInstances[i];
        auto prefix = "meshInstances[" + std::to_string(i) + "] in " + path + ": ";
        if (instance.gltfPath.empty()) {
            throw std::runtime_error(prefix + "missing 'gltf'");
        }
        if (instance.vertexShader.empty()) {
            throw std::runtime_error(prefix + "missing 'vertexShader'");
        }
        if (instance.fragmentShader.empty()) {
            throw std::runtime_error(prefix + "missing 'fragmentShader'");
        }
    }
}

void validateVolumetricBounds(VolumetricConfig const& config, std::string const& path) {
    if (!config.bounded) {
        return;
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(config.boundsMin[axis]) || !std::isfinite(config.boundsMax[axis]) ||
                config.boundsMin[axis] >= config.boundsMax[axis]) {
            throw std::runtime_error("volumetric.bounds must have min < max: " + path);
        }
    }
}

void validateVolumetric(Scene const& scene, std::string const& path) {
    if (!scene.volumetric) {
        return;
    }
    auto const& config = *scene.volumetric;
    validateVolumetricBounds(config, path);
    bool invalidDensity = !std::isfinite(config.density) || config.density < 0.0f;
    bool invalidCoefficients = !std::isfinite(config.absorption) || config.absorption < 0.0f ||
                               !std::isfinite(config.scattering) || config.scattering < 0.0f ||
                               !std::isfinite(config.absorption + config.scattering) ||
                               !std::isfinite(config.extinctionCoefficient());
    bool invalidDistance = !std::isfinite(config.maxDistance) || config.maxDistance <= 0.0f;
    bool invalidAnisotropy =
            !std::isfinite(config.anisotropy) || std::abs(config.anisotropy) >= 1.0f;
    bool invalidSteps = config.steps < 1 || config.steps > 256;
    if (invalidDensity || invalidCoefficients || invalidDistance || invalidAnisotropy ||
            invalidSteps) {
        throw std::runtime_error("invalid volumetric settings: " + path);
    }
}

void validateScene(Scene const& scene, std::string const& path) {
    validateAo(scene, path);
    validateCamera(scene.camera, path);
    validateVolumetric(scene, path);
    if (scene.meshInstances.empty() && !scene.particles) {
        throw std::runtime_error("scene has no meshInstances or particles: " + path);
    }
    validateLights(scene, path);
    validateMeshes(scene, path);
}

SaoConfig parseSao(json const& saoJson) {
    SaoConfig saoConfig;
    saoConfig.radius = saoJson.value("radius", saoConfig.radius);
    saoConfig.bias = saoJson.value("bias", saoConfig.bias);
    saoConfig.power = saoJson.value("power", saoConfig.power);
    saoConfig.intensity = saoJson.value("intensity", saoConfig.intensity);
    saoConfig.sampleCount = saoJson.value("sampleCount", saoConfig.sampleCount);
    saoConfig.spiralTurns = saoJson.value("spiralTurns", saoConfig.spiralTurns);
    saoConfig.kernelRadius = saoJson.value("kernelRadius", saoConfig.kernelRadius);
    saoConfig.depthThreshold = saoJson.value("depthThreshold", saoConfig.depthThreshold);
    return saoConfig;
}

GtaoConfig parseGtao(json const& gtaoJson) {
    GtaoConfig gtaoConfig;
    gtaoConfig.radius = gtaoJson.value("radius", gtaoConfig.radius);
    gtaoConfig.thicknessHeuristic =
            gtaoJson.value("thicknessHeuristic", gtaoConfig.thicknessHeuristic);
    gtaoConfig.power = gtaoJson.value("power", gtaoConfig.power);
    gtaoConfig.intensity = gtaoJson.value("intensity", gtaoConfig.intensity);
    gtaoConfig.stepCount = gtaoJson.value("stepCount", gtaoConfig.stepCount);
    gtaoConfig.directionCount = gtaoJson.value("directionCount", gtaoConfig.directionCount);
    gtaoConfig.kernelRadius = gtaoJson.value("kernelRadius", gtaoConfig.kernelRadius);
    gtaoConfig.depthThreshold = gtaoJson.value("depthThreshold", gtaoConfig.depthThreshold);
    return gtaoConfig;
}

void parseVolumetricBounds(json const& bounds, VolumetricConfig& config) {
    config.boundsMin = vec3FromJson(bounds.at("min"));
    config.boundsMax = vec3FromJson(bounds.at("max"));
    config.bounded = true;
}

VolumetricConfig parseVolumetric(json const& value) {
    VolumetricConfig config;
    config.density = value.value("density", config.density);
    config.absorption = value.value("absorption", config.absorption);
    config.scattering = value.value("scattering", config.scattering);
    config.maxDistance = value.value("maxDistance", config.maxDistance);
    config.anisotropy = value.value("anisotropy", config.anisotropy);
    config.steps = value.value("steps", config.steps);
    if (value.contains("bounds")) {
        parseVolumetricBounds(value.at("bounds"), config);
    }
    return config;
}

MeshInstance parseMeshInstance(json const& instanceJson) {
    MeshInstance instance;
    instance.gltfPath = instanceJson.at("gltf").get<std::string>();
    instance.texturePath = instanceJson.value("texture", instance.texturePath);
    instance.vertexShader = instanceJson.at("vertexShader").get<std::string>();
    instance.fragmentShader = instanceJson.at("fragmentShader").get<std::string>();
    if (instanceJson.contains("baseColor")) {
        auto const& baseColorJson = instanceJson["baseColor"];
        instance.baseColor = glm::vec4{ vec3FromJson(baseColorJson),
            baseColorJson.size() > 3 ? baseColorJson[3].get<float>() : 1.0f };
    }
    if (instanceJson.contains("position")) {
        instance.position = vec3FromJson(instanceJson["position"]);
    }
    if (instanceJson.contains("rotation")) {
        instance.rotation = vec3FromJson(instanceJson["rotation"]);
    }
    if (instanceJson.contains("scale")) {
        instance.scale = vec3FromJson(instanceJson["scale"]);
    }
    if (instanceJson.contains("metallic")) {
        instance.metallic = instanceJson.at("metallic").get<float>();
    }
    if (instanceJson.contains("roughness")) {
        instance.roughness = instanceJson.at("roughness").get<float>();
    }
    instance.useNormalMap = instanceJson.value("useNormalMap", instance.useNormalMap);
    instance.castShadows = instanceJson.value("castShadows", instance.castShadows);
    instance.receiveShadows = instanceJson.value("receiveShadows", instance.receiveShadows);
    return instance;
}

void requireLightFields(json const& lightJson, std::string const& lightType,
        std::initializer_list<std::string_view> fields, std::string const& path) {
    for (auto const& field: fields) {
        if (!lightJson.contains(field)) {
            throw std::runtime_error("light." + std::string(field) + " is required for " +
                                     lightType + " light: " + path);
        }
    }
}

AmbientLight parseAmbientLight(json const& lightJson, std::string const& path) {
    requireLightFields(lightJson, "ambient", { "intensity" }, path);
    AmbientLight light;
    light.intensity = lightJson["intensity"].get<float>();
    light.iblIntensity = lightJson.value("iblIntensity", light.iblIntensity);
    return light;
}

void parseShadowSettings(json const& lightJson, ShadowSettings& shadows, std::string const& path) {
    shadows.castShadow = lightJson.value("castShadow", shadows.castShadow);
    shadows.shadowBias = lightJson.value("shadowBias", shadows.shadowBias);
    if (lightJson.contains("shadowType")) {
        std::string shadowType = lightJson["shadowType"].get<std::string>();
        if (shadowType == "pcf") {
            shadows.shadowType = ShadowType::PCF;
        } else if (shadowType != "hard") {
            throw std::runtime_error("unknown shadowType '" + shadowType + "': " + path);
        }
    }
}

DirectionalLight parseDirectionalLight(json const& lightJson, std::string const& path) {
    requireLightFields(lightJson, "directional", { "direction", "color" }, path);
    DirectionalLight light;
    light.direction = vec3FromJson(lightJson["direction"]);
    light.color = vec3FromJson(lightJson["color"]);
    light.intensity = lightJson.value("intensity", light.intensity);
    parseShadowSettings(lightJson, light.shadows, path);
    return light;
}

SpotLight parseSpotLight(json const& lightJson, std::string const& path) {
    requireLightFields(lightJson, "spot",
            { "position", "direction", "color", "innerConeAngle", "outerConeAngle", "range" },
            path);
    SpotLight light;
    light.position = vec3FromJson(lightJson["position"]);
    light.direction = vec3FromJson(lightJson["direction"]);
    light.color = vec3FromJson(lightJson["color"]);
    light.intensity = lightJson.value("intensity", light.intensity);
    light.innerConeAngle = lightJson["innerConeAngle"].get<float>();
    light.outerConeAngle = lightJson["outerConeAngle"].get<float>();
    light.range = lightJson["range"].get<float>();
    parseShadowSettings(lightJson, light.shadows, path);
    return light;
}

PointLight parsePointLight(json const& lightJson, std::string const& path) {
    requireLightFields(lightJson, "point", { "position", "color", "range" }, path);
    PointLight light;
    light.position = vec3FromJson(lightJson["position"]);
    light.color = vec3FromJson(lightJson["color"]);
    light.range = lightJson["range"].get<float>();
    light.intensity = lightJson.value("intensity", light.intensity);
    return light;
}

Light parseLight(json const& lightJson, std::string const& path) {
    if (!lightJson.contains("type")) {
        throw std::runtime_error("light.type is required: " + path);
    }
    std::string lightType = lightJson.at("type").get<std::string>();
    if (lightType == "ambient") {
        return parseAmbientLight(lightJson, path);
    }
    if (lightType == "directional") {
        return parseDirectionalLight(lightJson, path);
    }
    if (lightType == "spot") {
        return parseSpotLight(lightJson, path);
    }
    if (lightType == "point") {
        return parsePointLight(lightJson, path);
    }
    throw std::runtime_error("unknown light.type '" + lightType + "': " + path);
}

void parseLights(json const& j, std::string const& path, Scene& scene) {
    if (j.contains("lights")) {
        for (auto const& lightJson: j["lights"]) {
            scene.lights.push_back(parseLight(lightJson, path));
        }
    } else if (j.contains("light")) {
        scene.lights.push_back(parseLight(j["light"], path));
    }
}

void parseCamera(json const& cameraJson, Camera& camera) {
    camera.target = dvec3FromJson(cameraJson.at("target"));
    camera.azimuth = cameraJson.at("azimuth").get<double>();
    camera.elevation = cameraJson.at("elevation").get<double>();
    camera.radius = cameraJson.at("radius").get<double>();
    camera.fovDegrees = cameraJson.at("fov").get<double>();
    camera.nearPlane = cameraJson.at("near").get<double>();
    camera.farPlane = cameraJson.at("far").get<double>();
    camera.aperture = cameraJson.value("aperture", camera.aperture);
    camera.shutterSpeed = cameraJson.value("shutterSpeed", camera.shutterSpeed);
    camera.sensitivity = cameraJson.value("sensitivity", camera.sensitivity);
}

ToneMapping parseToneMapping(json const& value, std::string const& path) {
    std::string mode = value.get<std::string>();
    if (mode == "reinhard") {
        return ToneMapping::Reinhard;
    }
    if (mode == "none") {
        return ToneMapping::None;
    }
    throw std::runtime_error("unknown toneMapping '" + mode + "': " + path);
}

} // namespace

Scene loadScene(std::string const& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open scene file: " + path);
    }
    json j = json::parse(file);
    Scene scene;
    if (j.contains("toneMapping")) {
        scene.toneMapping = parseToneMapping(j.at("toneMapping"), path);
    }
    if (j.contains("ibl")) {
        scene.iblPath = j["ibl"].get<std::string>();
    }
    scene.skybox = j.value("skybox", scene.skybox);
    if (j.contains("sao")) {
        scene.sao = parseSao(j["sao"]);
    }
    if (j.contains("gtao")) {
        scene.gtao = parseGtao(j["gtao"]);
    }
    if (j.contains("volumetric")) {
        scene.volumetric = parseVolumetric(j.at("volumetric"));
    }

    for (auto const& instanceJson: j.at("meshInstances")) {
        scene.meshInstances.push_back(parseMeshInstance(instanceJson));
    }
    if (j.contains("particles")) {
        ParticleSystem particles;
        particles.count = j["particles"].value("count", particles.count);
        scene.particles = particles;
    }
    parseLights(j, path, scene);
    parseCamera(j.at("camera"), scene.camera);

    validateScene(scene, path);
    return scene;
}
