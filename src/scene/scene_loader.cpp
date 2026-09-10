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
    auto const& camera = scene.camera;
    if (camera.radius <= 0.0) throw std::runtime_error("camera.radius must be > 0: " + path);
    if (camera.fovDegrees <= 0.0 || camera.fovDegrees >= 180.0)
        throw std::runtime_error("camera.fov must be in (0, 180): " + path);
    if (camera.nearPlane <= 0.0) throw std::runtime_error("camera.near must be > 0: " + path);
    if (camera.farPlane <= camera.nearPlane)
        throw std::runtime_error("camera.far must be > camera.near: " + path);
    if (camera.elevation <= -90.0 || camera.elevation >= 90.0)
        throw std::runtime_error("camera.elevation must be in (-90, 90): " + path);

    if (scene.meshInstances.empty() && !scene.particles)
        throw std::runtime_error("scene has no meshInstances or particles: " + path);

    for (size_t i = 0; i < scene.meshInstances.size(); ++i) {
        auto const& instance = scene.meshInstances[i];
        auto prefix = "meshInstances[" + std::to_string(i) + "] in " + path + ": ";
        if (instance.gltfPath.empty()) throw std::runtime_error(prefix + "missing 'gltf'");
        if (instance.vertexShader.empty())
            throw std::runtime_error(prefix + "missing 'vertexShader'");
        if (instance.fragmentShader.empty())
            throw std::runtime_error(prefix + "missing 'fragmentShader'");
    }
}

Scene loadScene(std::string const& path) {
    std::ifstream file(path);
    if (!file.is_open()) throw std::runtime_error("failed to open scene file: " + path);

    json j = json::parse(file);

    Scene scene;

    for (auto const& instanceJson: j.at("meshInstances")) {
        MeshInstance instance;
        instance.gltfPath = instanceJson.at("gltf").get<std::string>();
        if (instanceJson.contains("texture"))
            instance.texturePath = instanceJson.at("texture").get<std::string>();
        instance.vertexShader = instanceJson.at("vertexShader").get<std::string>();
        instance.fragmentShader = instanceJson.at("fragmentShader").get<std::string>();
        if (instanceJson.contains("baseColor")) {
            auto const& baseColorJson = instanceJson["baseColor"];
            instance.baseColor = { vec3FromJson(baseColorJson),
                baseColorJson.size() > 3 ? baseColorJson[3].get<float>() : 1.0f };
        }
        if (instanceJson.contains("position"))
            instance.position = vec3FromJson(instanceJson["position"]);
        if (instanceJson.contains("rotation"))
            instance.rotation = vec3FromJson(instanceJson["rotation"]);
        if (instanceJson.contains("scale")) instance.scale = vec3FromJson(instanceJson["scale"]);
        if (instanceJson.contains("metallic"))
            instance.metallic = instanceJson["metallic"].get<float>();
        if (instanceJson.contains("roughness"))
            instance.roughness = instanceJson["roughness"].get<float>();
        scene.meshInstances.push_back(std::move(instance));
    }

    if (j.contains("particles")) {
        ParticleSystem particles;
        particles.count = j["particles"].value("count", 8192u);
        scene.particles = particles;
    }

    if (j.contains("light")) {
        auto const& lightJson = j["light"];
        if (!lightJson.contains("direction"))
            throw std::runtime_error("light.direction is required: " + path);
        if (!lightJson.contains("color"))
            throw std::runtime_error("light.color is required: " + path);
        DirectionalLight light;
        light.direction = vec3FromJson(lightJson["direction"]);
        light.color = vec3FromJson(lightJson["color"]);
        scene.light = light;
    }

    auto const& cameraJson = j.at("camera");
    scene.camera.target = dvec3FromJson(cameraJson.at("target"));
    scene.camera.azimuth = cameraJson.at("azimuth").get<double>();
    scene.camera.elevation = cameraJson.at("elevation").get<double>();
    scene.camera.radius = cameraJson.at("radius").get<double>();
    scene.camera.fovDegrees = cameraJson.at("fov").get<double>();
    scene.camera.nearPlane = cameraJson.at("near").get<double>();
    scene.camera.farPlane = cameraJson.at("far").get<double>();

    validateScene(scene, path);
    return scene;
}
