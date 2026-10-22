#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL

#include "medium.glsl"

struct GpuLight {
    vec4 colorAndType;           // xyz=RGB intensity, w=type (1=directional,2=spot,3=point)
    vec4 positionAndInvRange;    // xyz=world position, w=1/range (spot/point)
    vec4 directionAndShadowFlag; // xyz=normalized direction, w=castsShadow
    vec4 coneScaleOffset;        // x=scale, y=offset for cone attenuation (spot only)
};

struct LightSample {
    vec3 L;
    float attenuation;
    float distance;
};

float windowedFalloff(float distSq, float invRange) {
    float factor = distSq * invRange * invRange;
    float smoothF = max(1.0 - factor * factor, 0.0);
    return (smoothF * smoothF) / max(distSq, 1e-4);
}

LightSample sampleLight(GpuLight light, vec3 worldPosition) {
    int lightType = int(light.colorAndType.w);
    if (lightType == 1) {
        return LightSample(light.directionAndShadowFlag.xyz, 1.0, MEDIUM_MAX_RAY_DISTANCE);
    }

    vec3 toLight = light.positionAndInvRange.xyz - worldPosition;
    float distSq = dot(toLight, toLight);
    float distance = sqrt(distSq);
    vec3 L = toLight / max(distance, 1e-4);
    float attenuation = windowedFalloff(distSq, light.positionAndInvRange.w);
    if (lightType == 2) {
        float cosTheta = dot(-L, light.directionAndShadowFlag.xyz);
        float cone = clamp(cosTheta * light.coneScaleOffset.x + light.coneScaleOffset.y, 0.0, 1.0);
        attenuation *= cone * cone;
    }
    return LightSample(L, attenuation, distance);
}

#endif
