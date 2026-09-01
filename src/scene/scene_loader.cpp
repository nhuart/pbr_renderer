#include "scene/scene_loader.hpp"

#include <fstream>
#include <stdexcept>

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
        MeshInstanceDesc desc;
        desc.gltfPath = inst.at("gltf").get<std::string>();
        desc.texturePath = inst.at("texture").get<std::string>();
        if (inst.contains("position")) desc.position = vecFromJson(inst["position"]);
        if (inst.contains("rotation")) desc.rotation = vecFromJson(inst["rotation"]);
        if (inst.contains("scale")) desc.scale = vecFromJson(inst["scale"]);
        scene.meshInstances.push_back(std::move(desc));
    }

    if (j.contains("particles")) {
        ParticleSystemDesc p;
        p.count = j["particles"].value("count", 8192u);
        scene.particles = p;
    }

    return scene;
}
