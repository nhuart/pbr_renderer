#include "scene/scene_loader.hpp"

#include <fstream>
#include <stdexcept>

#include "types.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static glm::vec3 vecFromJson(json const& j) {
    return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
}

static void validateScene(Scene const& scene, std::string const& path) {
    if (!scene.camera)
        throw std::runtime_error("scene missing 'camera': " + path);

    auto const& cam = *scene.camera;
    if (cam.radius <= 0.0)
        throw std::runtime_error("camera.radius must be > 0: " + path);
    if (cam.fovDegrees <= 0.0 || cam.fovDegrees >= 180.0)
        throw std::runtime_error("camera.fov must be in (0, 180): " + path);
    if (cam.nearPlane <= 0.0)
        throw std::runtime_error("camera.near must be > 0: " + path);
    if (cam.farPlane <= cam.nearPlane)
        throw std::runtime_error("camera.far must be > camera.near: " + path);
    if (cam.elevation <= -90.0 || cam.elevation >= 90.0)
        throw std::runtime_error("camera.elevation must be in (-90, 90): " + path);

    if (scene.meshInstances.empty() && !scene.particles)
        throw std::runtime_error("scene has no meshInstances or particles: " + path);

    for (size_t i = 0; i < scene.meshInstances.size(); ++i) {
        auto const& inst = scene.meshInstances[i];
        auto prefix = "meshInstances[" + std::to_string(i) + "] in " + path + ": ";
        if (inst.gltfPath.empty())
            throw std::runtime_error(prefix + "missing 'gltf'");
        if (inst.vertexShader.empty())
            throw std::runtime_error(prefix + "missing 'vertexShader'");
        if (inst.fragmentShader.empty())
            throw std::runtime_error(prefix + "missing 'fragmentShader'");
    }
}

Scene loadScene(std::string const& path) {
    std::ifstream file(path);
    if (!file.is_open()) throw std::runtime_error("failed to open scene file: " + path);

    json j = json::parse(file);

    Scene scene;

    for (auto const& inst: j.at("meshInstances")) {
        MeshInstance meshInstance;
        meshInstance.gltfPath = inst.at("gltf").get<std::string>();
        if (inst.contains("texture")) meshInstance.texturePath = inst.at("texture").get<std::string>();
        meshInstance.vertexShader = inst.at("vertexShader").get<std::string>();
        meshInstance.fragmentShader = inst.at("fragmentShader").get<std::string>();
        if (inst.contains("baseColor")) {
            auto const& c = inst["baseColor"];
            meshInstance.baseColor = { c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                c.size() > 3 ? c[3].get<float>() : 1.0f };
        }
        if (inst.contains("position")) meshInstance.position = vecFromJson(inst["position"]);
        if (inst.contains("rotation")) meshInstance.rotation = vecFromJson(inst["rotation"]);
        if (inst.contains("scale")) meshInstance.scale = vecFromJson(inst["scale"]);
        if (inst.contains("metallic")) meshInstance.metallic = inst["metallic"].get<float>();
        if (inst.contains("roughness")) meshInstance.roughness = inst["roughness"].get<float>();
        scene.meshInstances.push_back(std::move(meshInstance));
    }

    if (j.contains("particles")) {
        ParticleSystem p;
        p.count = j["particles"].value("count", 8192u);
        scene.particles = p;
    }

    {
        auto const& c = j.at("camera");
        auto const& t = c.at("target");
        Camera cam;
        cam.target    = { t[0].get<double>(), t[1].get<double>(), t[2].get<double>() };
        cam.azimuth   = c.at("azimuth").get<double>();
        cam.elevation = c.at("elevation").get<double>();
        cam.radius    = c.at("radius").get<double>();
        cam.fovDegrees = c.at("fov").get<double>();
        cam.nearPlane = c.at("near").get<double>();
        cam.farPlane  = c.at("far").get<double>();
        scene.camera  = cam;
    }

    validateScene(scene, path);
    return scene;
}
