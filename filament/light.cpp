#include "light.h"

#include <filament/LightManager.h>
#include <math/vec3.h>
#include <utils/EntityManager.h>

#include <cmath>

static constexpr float kDegToRad = float(M_PI) / 180.0f;

static float3 normalizeDirection(float3 v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return len > 0.0f ? float3{ v.x / len, v.y / len, v.z / len } : v;
}

void createLights(filament::Engine& engine, filament::Scene& scene,
        std::vector<LightDesc> const& lights, std::vector<utils::Entity>& outEntities) {
    for (auto const& desc: lights) {
        utils::Entity entity = utils::EntityManager::get().create();

        auto builder = filament::LightManager::Builder(desc.type)
                               .color(desc.color)
                               .castShadows(desc.castShadows);

        switch (desc.type) {
            case filament::LightManager::Type::DIRECTIONAL:
            case filament::LightManager::Type::SUN:
                builder.direction(-normalizeDirection(desc.direction)).intensity(desc.intensity);
                break;
            case filament::LightManager::Type::POINT:
                builder.position(desc.position).intensity(desc.intensity).falloff(desc.range);
                break;
            case filament::LightManager::Type::SPOT:
            case filament::LightManager::Type::FOCUSED_SPOT:
                builder.position(desc.position)
                        .direction(normalizeDirection(desc.direction))
                        .intensity(desc.intensity)
                        .falloff(desc.range)
                        .spotLightCone(desc.innerConeAngleDegrees * kDegToRad,
                                desc.outerConeAngleDegrees * kDegToRad);
                break;
        }

        builder.build(engine, entity);
        scene.addEntity(entity);
        outEntities.push_back(entity);
    }
}

void destroyLights(filament::Engine& engine, std::vector<utils::Entity>& entities) {
    auto& lightManager = engine.getLightManager();
    for (auto entity: entities) {
        lightManager.destroy(entity);
    }
    utils::EntityManager::get().destroy(entities.size(), entities.data());
    entities.clear();
}
