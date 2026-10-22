#version 450
#extension GL_GOOGLE_include_directive : require

#include "medium.glsl"
#include "lighting.glsl"

layout(std140, binding = 0) uniform VolumetricUBO {
    mat4 invViewProj;
    mat4 lightSpaceMatrix;
    vec4 cameraPos;
    GpuLight light;
    float extinctionCoefficient;
    float scatteringAlbedo;
    float maxDistance;
    float anisotropy;
    vec4 boundsMin; // w = 1 for a bounded participating medium
    vec4 boundsMax;
    int stepCount;
    float shadowBias;
} ubo;

layout(binding = 1) uniform sampler2D sceneDepth;
layout(binding = 2) uniform sampler2D shadowMap;
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;
const float MIN_TRANSMITTANCE = 1e-3;
const float FAR_PLANE_DEPTH = 1.0;
const vec2 JITTER_HASH_WEIGHTS = vec2(12.9898, 78.233);
const float JITTER_HASH_SCALE = 43758.5453;

struct MediumIntegration {
    float scattering;
    float transmittance;
};

bool isBoundedMedium() {
    return ubo.boundsMin.w != 0.0;
}

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 worldPosition = ubo.invViewProj * vec4(uv * 2.0 - 1.0, depth, 1.0);
    return worldPosition.xyz / worldPosition.w;
}

RaySegment createViewRay() {
    vec3 origin = ubo.cameraPos.xyz;
    vec3 direction = normalize(reconstructWorldPosition(inUV, FAR_PLANE_DEPTH) - origin);
    float surfaceDepth = texture(sceneDepth, inUV).r;
    float endDistance = ubo.maxDistance;
    if (surfaceDepth < FAR_PLANE_DEPTH) {
        float surfaceDistance = length(reconstructWorldPosition(inUV, surfaceDepth) - origin);
        endDistance = min(endDistance, surfaceDistance);
    }
    return RaySegment(origin, direction, 0.0, endDistance);
}

bool intersectMediumBounds(inout RaySegment ray) {
    if (!isBoundedMedium()) {
        return true;
    }
    return intersectBoundedMedium(ubo.boundsMin.xyz, ubo.boundsMax.xyz, ray);
}

bool isInsideShadowCoverage(vec3 lightNdcPosition) {
    return lightNdcPosition.z > 0.0 && lightNdcPosition.z < FAR_PLANE_DEPTH &&
           !any(greaterThan(abs(lightNdcPosition.xy), vec2(1.0)));
}

bool isVisibleToLight(vec3 worldPosition) {
    vec4 lightClipPosition = ubo.lightSpaceMatrix * vec4(worldPosition, 1.0);
    if (lightClipPosition.w <= 0.0) {
        return true;
    }
    vec3 lightNdcPosition = lightClipPosition.xyz / lightClipPosition.w;
    if (!isInsideShadowCoverage(lightNdcPosition)) {
        return true; // Outside shadow coverage, no occluder is known; medium bounds are separate.
    }
    vec2 shadowUv = lightNdcPosition.xy * 0.5 + 0.5;
    float closestOccluderDepth = texture(shadowMap, shadowUv).r;
    return lightNdcPosition.z - ubo.shadowBias <= closestOccluderDepth;
}

float transmittanceFromLight(vec3 worldPosition, LightSample light) {
    // Match the surface pass's unattenuated incident-light model for unbounded fog.
    if (!isBoundedMedium()) {
        return 1.0;
    }
    // The source outside the bounded medium is assumed to have unit medium transmittance.
    RaySegment lightRay = RaySegment(worldPosition, light.L, 0.0, light.distance);
    return boundedMediumTransmittance(lightRay, ubo.boundsMin.xyz, ubo.boundsMax.xyz,
            ubo.extinctionCoefficient);
}

float evaluateHenyeyGreensteinPhase(float scatteringCosine, float anisotropy) {
    float cosine = clamp(scatteringCosine, -1.0, 1.0);
    float anisotropyMagnitude = abs(anisotropy);
    float alignedCosine = anisotropy >= 0.0 ? cosine : -cosine;
    float anisotropyGap = 1.0 - anisotropyMagnitude;
    // Factor HG to avoid cancellation near |g| = 1 without clamping away scattered energy.
    float phaseBase = anisotropyGap * anisotropyGap +
                      2.0 * anisotropyMagnitude * (1.0 - alignedCosine);
    return (anisotropyGap * (1.0 + anisotropyMagnitude)) /
           (4.0 * PI * phaseBase * sqrt(phaseBase));
}

float calculateSampleJitter(vec2 pixelCoordinates) {
    // Offset ray-march samples per pixel to replace visible bands with spatial noise.
    return fract(sin(dot(floor(pixelCoordinates), JITTER_HASH_WEIGHTS)) * JITTER_HASH_SCALE);
}

float sampleScatteringWeight(vec3 worldPosition, vec3 viewDirection) {
    LightSample light = sampleLight(ubo.light, worldPosition);
    if (light.attenuation <= 0.0 || !isVisibleToLight(worldPosition)) {
        return 0.0;
    }
    // A spotlight's direction to the source, falloff and phase vary along the view ray.
    float phase = evaluateHenyeyGreensteinPhase(dot(viewDirection, light.L), ubo.anisotropy);
    return light.attenuation * phase * transmittanceFromLight(worldPosition, light);
}

MediumIntegration integrateMedium(RaySegment ray) {
    float pathLength = ray.endDistance - ray.startDistance;
    float stepLength = pathLength / float(ubo.stepCount);
    float segmentOpticalDepth = ubo.extinctionCoefficient * stepLength;
    float segmentTransmittance = exp(-segmentOpticalDepth);
    float sampleJitter = calculateSampleJitter(gl_FragCoord.xy);
    float transmittance = 1.0;
    float scattering = 0.0;
    for (int i = 0; i < ubo.stepCount; ++i) {
        float distanceAlongRay = ray.startDistance + (float(i) + sampleJitter) * stepLength;
        vec3 samplePosition = ray.origin + ray.direction * distanceAlongRay;
        // Weight scattering by transmittance after attenuation through this segment.
        transmittance *= segmentTransmittance;
        if (ubo.scatteringAlbedo > 0.0) {
            scattering += transmittance * segmentOpticalDepth *
                          sampleScatteringWeight(samplePosition, ray.direction);
        }
        if (transmittance < MIN_TRANSMITTANCE) {
            break;
        }
    }
    // Background attenuation covers the full medium path even when scattering stops early.
    float fullPathTransmittance = exp(-ubo.extinctionCoefficient * pathLength);
    return MediumIntegration(scattering, fullPathTransmittance);
}

void main() {
    RaySegment viewRay = createViewRay();
    if (!intersectMediumBounds(viewRay)) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    MediumIntegration medium = integrateMedium(viewRay);
    outColor = vec4(ubo.light.colorAndType.rgb * (ubo.scatteringAlbedo * medium.scattering),
            medium.transmittance);
}
