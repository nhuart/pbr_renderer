#include "scene/scene_loader.hpp"

#include <fstream>
#include <stdexcept>

#include "types.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static glm::vec3 vec3FromJson(json const& j) {
    return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
}

static glm::dvec3 dvec3FromJson(json const& j) {
    return { j[0].get<double>(), j[1].get<double>(), j[2].get<double>() };
}

static void validateScene(Scene const& scene, std::string const& path) {
    if (scene.sao && scene.gtao) {
        throw std::runtime_error("scene cannot enable both sao and gtao: " + path);
    }
    if (scene.gtao) {
        auto const& g = *scene.gtao;
        bool invalidRadius = g.radius <= 0.0f;
        bool invalidThickness = g.thicknessHeuristic < 0.0f || g.thicknessHeuristic > 1.0f;
        bool invalidSampling = g.stepCount < 1 || g.directionCount < 1;
        bool invalidResponse = g.power <= 0.0f || g.intensity < 0.0f;
        bool invalidBlur = g.kernelRadius < 0 || g.depthThreshold <= 0.0f;
        bool invalidSettings = invalidRadius || invalidThickness || invalidSampling ||
                               invalidResponse || invalidBlur;
        if (invalidSettings) {
            throw std::runtime_error("invalid gtao settings: " + path);
        }
    }
    auto const& camera = scene.camera;
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

    if (scene.meshInstances.empty() && !scene.particles) {
        throw std::runtime_error("scene has no meshInstances or particles: " + path);
    }

    int shadowDirLightCount = 0;
    for (auto const& light: scene.lights) {
        if (auto const* dl = std::get_if<DirectionalLight>(&light)) {
            if (dl->castShadow) {
                ++shadowDirLightCount;
            }
        }
    }
    if (shadowDirLightCount > 1) {
        throw std::runtime_error(
                "scene has " + std::to_string(shadowDirLightCount) +
                " shadow-casting directional lights — only one is supported: " + path);
    }

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
    if (j.contains("skybox")) {
        scene.skybox = j["skybox"].get<bool>();
    }
    if (j.contains("sao")) {
        auto const& saoJson = j["sao"];
        SaoConfig saoConfig;
        saoConfig.radius = saoJson.value("radius", saoConfig.radius);
        saoConfig.bias = saoJson.value("bias", saoConfig.bias);
        saoConfig.power = saoJson.value("power", saoConfig.power);
        saoConfig.intensity = saoJson.value("intensity", saoConfig.intensity);
        saoConfig.sampleCount = saoJson.value("sampleCount", saoConfig.sampleCount);
        saoConfig.spiralTurns = saoJson.value("spiralTurns", saoConfig.spiralTurns);
        saoConfig.kernelRadius = saoJson.value("kernelRadius", saoConfig.kernelRadius);
        saoConfig.depthThreshold = saoJson.value("depthThreshold", saoConfig.depthThreshold);
        scene.sao = saoConfig;
    }
    if (j.contains("gtao")) {
        auto const& gtaoJson = j["gtao"];
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
        scene.gtao = gtaoConfig;
    }


    for (auto const& instanceJson: j.at("meshInstances")) {
        MeshInstance instance;
        instance.gltfPath = instanceJson.at("gltf").get<std::string>();
        if (instanceJson.contains("texture")) {
            instance.texturePath = instanceJson.at("texture").get<std::string>();
        }
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
        if (instanceJson.contains("metallic")) {
            instance.metallic = instanceJson["metallic"].get<float>();
        }
        if (instanceJson.contains("roughness")) {
            instance.roughness = instanceJson["roughness"].get<float>();
        }
        if (instanceJson.contains("useNormalMap")) {
            instance.useNormalMap = instanceJson["useNormalMap"].get<bool>();
        }
        if (instanceJson.contains("castShadows")) {
            instance.castShadows = instanceJson["castShadows"].get<bool>();
        }
        if (instanceJson.contains("receiveShadows")) {
            instance.receiveShadows = instanceJson["receiveShadows"].get<bool>();
        }
        scene.meshInstances.push_back(std::move(instance));
    }

    if (j.contains("particles")) {
        ParticleSystem particles;
        particles.count = j["particles"].value("count", 8192u);
        scene.particles = particles;
    }

    auto parseLight = [&](json const& lightJson) -> Light {
        if (!lightJson.contains("type")) {
            throw std::runtime_error("light.type is required: " + path);
        }
        std::string lightType = lightJson.at("type").get<std::string>();

        auto requireFields = [&](std::initializer_list<std::string_view> fields) {
            for (auto const& field: fields) {
                if (!lightJson.contains(field)) {
                    throw std::runtime_error("light." + std::string(field) + " is required for " +
                                             lightType + " light: " + path);
                }
            }
        };

        if (lightType == "ambient") {
            requireFields({ "intensity" });
            AmbientLight light;
            light.intensity = lightJson["intensity"].get<float>();
            if (lightJson.contains("iblIntensity")) {
                light.iblIntensity = lightJson["iblIntensity"].get<float>();
            }
            return light;
        } else if (lightType == "directional") {
            requireFields({ "direction", "color" });
            DirectionalLight light;
            light.direction = vec3FromJson(lightJson["direction"]);
            light.color = vec3FromJson(lightJson["color"]);
            if (lightJson.contains("intensity")) {
                light.intensity = lightJson["intensity"].get<float>();
            }
            if (lightJson.contains("castShadow")) {
                light.castShadow = lightJson["castShadow"].get<bool>();
            }
            if (lightJson.contains("shadowBias")) {
                light.shadowBias = lightJson["shadowBias"].get<float>();
            }
            if (lightJson.contains("shadowType")) {
                std::string st = lightJson["shadowType"].get<std::string>();
                if (st == "pcf") {
                    light.shadowType = ShadowType::PCF;
                } else if (st != "hard") {
                    throw std::runtime_error("unknown shadowType '" + st + "': " + path);
                }
            }
            return light;
        } else if (lightType == "spot") {
            requireFields({ "position", "direction", "color", "innerConeAngle", "outerConeAngle",
                "range" });
            SpotLight light;
            light.position = vec3FromJson(lightJson["position"]);
            light.direction = vec3FromJson(lightJson["direction"]);
            light.color = vec3FromJson(lightJson["color"]);
            if (lightJson.contains("intensity")) {
                light.intensity = lightJson["intensity"].get<float>();
            }
            light.innerConeAngle = lightJson["innerConeAngle"].get<float>();
            light.outerConeAngle = lightJson["outerConeAngle"].get<float>();
            light.range = lightJson["range"].get<float>();
            return light;
        } else if (lightType == "point") {
            requireFields({ "position", "color", "range" });
            PointLight light;
            light.position = vec3FromJson(lightJson["position"]);
            light.color = vec3FromJson(lightJson["color"]);
            light.range = lightJson["range"].get<float>();
            if (lightJson.contains("intensity")) {
                light.intensity = lightJson["intensity"].get<float>();
            }
            return light;
        } else {
            throw std::runtime_error("unknown light.type '" + lightType + "': " + path);
        }
    };

    if (j.contains("lights")) {
        for (auto const& lightJson: j["lights"]) {
            scene.lights.push_back(parseLight(lightJson));
        }
    } else if (j.contains("light")) {
        scene.lights.push_back(parseLight(j["light"]));
    }

    auto const& cameraJson = j.at("camera");
    scene.camera.target = dvec3FromJson(cameraJson.at("target"));
    scene.camera.azimuth = cameraJson.at("azimuth").get<double>();
    scene.camera.elevation = cameraJson.at("elevation").get<double>();
    scene.camera.radius = cameraJson.at("radius").get<double>();
    scene.camera.fovDegrees = cameraJson.at("fov").get<double>();
    scene.camera.nearPlane = cameraJson.at("near").get<double>();
    scene.camera.farPlane = cameraJson.at("far").get<double>();
    if (cameraJson.contains("aperture")) {
        scene.camera.aperture = cameraJson["aperture"].get<double>();
    }
    if (cameraJson.contains("shutterSpeed")) {
        scene.camera.shutterSpeed = cameraJson["shutterSpeed"].get<double>();
    }
    if (cameraJson.contains("sensitivity")) {
        scene.camera.sensitivity = cameraJson["sensitivity"].get<double>();
    }

    validateScene(scene, path);
    return scene;
}
