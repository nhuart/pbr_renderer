#pragma once

#include <filament/Engine.h>
#include <filament/LightManager.h>
#include <filament/Options.h>
#include <filament/Scene.h>
#include <math/vec3.h>

#include <vector>

using filament::math::float3;

struct LightDesc {
    filament::LightManager::Type type;
    float3 direction = { 0.0f, -1.0f, 0.0f };
    float3 position = { 0.0f, 0.0f, 0.0f };
    float3 color = { 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
    float range = 10.0f;
    float innerConeAngleDegrees = 0.0f;
    float outerConeAngleDegrees = 45.0f;
    bool castShadows = false;
};

void createLights(filament::Engine& engine, filament::Scene& scene,
        std::vector<LightDesc> const& lights, std::vector<utils::Entity>& outEntities);

void destroyLights(filament::Engine& engine, std::vector<utils::Entity>& entities);
