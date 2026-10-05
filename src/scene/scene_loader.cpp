#include "scene/scene_loader.hpp"

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

void validateLights(Scene const& scene, std::string const& path) {
    int shadowDirLightCount = 0;
    for (auto const& light: scene.lights) {
        if (auto const* directionalLight = std::get_if<DirectionalLight>(&light)) {
            if (directionalLight->castShadow) {
                ++shadowDirLightCount;
            }
        }
    }
    if (shadowDirLightCount > 1) {
        throw std::runtime_error(
                "scene has " + std::to_string(shadowDirLightCount) +
                " shadow-casting directional lights — only one is supported: " + path);
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

void validateScene(Scene const& scene, std::string const& path) {
    validateAo(scene, path);
    validateCamera(scene.camera, path);
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

MeshInstance parseMeshInstance(json const& instanceJson) {
    MeshInstance instance;
    instance.gltfPath = instanceJson.at("gltf").get<std::string>();
    instance.texturePath = instanceJson.value("texture", instance.texturePath);
    instance.vertexShader = instanceJson.at("vertexShader").get<std::string>();
    instance.fragmentShader = instanceJson.at("fragmentShader").get<std::string>();
    if (instanceJson.contains("baseColor")) {
        auto const& baseColorJson = instanceJson["baseColor"];
        instance.baseColor = { vec3FromJson(baseColorJson),
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
    instance.metallic = instanceJson.value("metallic", instance.metallic);
    instance.roughness = instanceJson.value("roughness", instance.roughness);
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

DirectionalLight parseDirectionalLight(json const& lightJson, std::string const& path) {
    requireLightFields(lightJson, "directional", { "direction", "color" }, path);
    DirectionalLight light;
    light.direction = vec3FromJson(lightJson["direction"]);
    light.color = vec3FromJson(lightJson["color"]);
    light.intensity = lightJson.value("intensity", light.intensity);
    light.castShadow = lightJson.value("castShadow", light.castShadow);
    light.shadowBias = lightJson.value("shadowBias", light.shadowBias);
    if (lightJson.contains("shadowType")) {
        std::string shadowType = lightJson["shadowType"].get<std::string>();
        if (shadowType == "pcf") {
            light.shadowType = ShadowType::PCF;
        } else if (shadowType != "hard") {
            throw std::runtime_error("unknown shadowType '" + shadowType + "': " + path);
        }
    }
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

} // namespace

Scene loadScene(std::string const& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open scene file: " + path);
    }
    json j = json::parse(file);
    Scene scene;
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
