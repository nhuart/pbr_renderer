#include "scene/scene_loader.hpp"

#include <fstream>
#include <stdexcept>

#include "types.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static glm::vec3 vecFromJson(json const& j) {
    return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
}

Scene loadScene(std::string const& path) {
    std::ifstream file(path);
    if (!file.is_open()) throw std::runtime_error("failed to open scene file: " + path);

    json j = json::parse(file);

    Scene scene;

    for (auto const& inst: j.at("meshInstances")) {
        MeshInstance meshInstance;
        meshInstance.gltfPath = inst.at("gltf").get<std::string>();
        meshInstance.texturePath = inst.at("texture").get<std::string>();
        if (inst.contains("position")) meshInstance.position = vecFromJson(inst["position"]);
        if (inst.contains("rotation")) meshInstance.rotation = vecFromJson(inst["rotation"]);
        if (inst.contains("scale")) meshInstance.scale = vecFromJson(inst["scale"]);
        scene.meshInstances.push_back(std::move(meshInstance));
    }

    if (j.contains("particles")) {
        ParticleSystem p;
        p.count = j["particles"].value("count", 8192u);
        scene.particles = p;
    }

    return scene;
}
