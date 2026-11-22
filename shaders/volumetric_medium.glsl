#ifndef VOLUMETRIC_MEDIUM_GLSL
#define VOLUMETRIC_MEDIUM_GLSL

#include "medium.glsl"
#include "lighting.glsl"

#ifndef VOLUMETRIC_DESCRIPTOR_SET
#define VOLUMETRIC_DESCRIPTOR_SET 0
#endif

layout(std140, set = VOLUMETRIC_DESCRIPTOR_SET, binding = 0) uniform VolumetricUBO {
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
} volume;

layout(set = VOLUMETRIC_DESCRIPTOR_SET, binding = 2) uniform sampler2D volumeShadowMap;

const float VOLUME_PI = 3.14159265359;
const float MIN_TRANSMITTANCE = 1e-3;
const float FAR_PLANE_DEPTH = 1.0;
const vec2 JITTER_HASH_WEIGHTS = vec2(12.9898, 78.233);
const float JITTER_HASH_SCALE = 43758.5453;

struct MediumIntegration {
    float scattering;
    float transmittance;
};

bool isBoundedMedium() {
    return volume.boundsMin.w != 0.0;
}

bool intersectMediumBounds(inout RaySegment ray) {
    if (!isBoundedMedium()) {
        return true;
    }
    return intersectBoundedMedium(volume.boundsMin.xyz, volume.boundsMax.xyz, ray);
}

bool isInsideShadowCoverage(vec3 lightNdcPosition) {
    return lightNdcPosition.z > 0.0 && lightNdcPosition.z < FAR_PLANE_DEPTH &&
           !any(greaterThan(abs(lightNdcPosition.xy), vec2(1.0)));
}

bool isVisibleToLight(vec3 worldPosition) {
    vec4 lightClipPosition = volume.lightSpaceMatrix * vec4(worldPosition, 1.0);
    if (lightClipPosition.w <= 0.0) {
        return true;
    }
    vec3 lightNdcPosition = lightClipPosition.xyz / lightClipPosition.w;
    if (!isInsideShadowCoverage(lightNdcPosition)) {
        return true; // Shadow coverage and medium bounds are separate.
    }
    vec2 shadowUv = lightNdcPosition.xy * 0.5 + 0.5;
    float closestOccluderDepth = texture(volumeShadowMap, shadowUv).r;
    return lightNdcPosition.z - volume.shadowBias <= closestOccluderDepth;
}

float transmittanceFromLight(vec3 worldPosition, LightSample light) {
    // Preserve the surface pass's unattenuated incident light for unbounded fog.
    if (!isBoundedMedium()) {
        return 1.0;
    }
    RaySegment lightRay = RaySegment(worldPosition, light.L, 0.0, light.distance);
    return boundedMediumTransmittance(lightRay, volume.boundsMin.xyz, volume.boundsMax.xyz,
            volume.extinctionCoefficient);
}

float evaluateHenyeyGreensteinPhase(float scatteringCosine, float anisotropy) {
    float cosine = clamp(scatteringCosine, -1.0, 1.0);
    float anisotropyMagnitude = abs(anisotropy);
    float alignedCosine = anisotropy >= 0.0 ? cosine : -cosine;
    float anisotropyGap = 1.0 - anisotropyMagnitude;
    // Factoring HG avoids cancellation near |g| = 1 without clamping scattered energy.
    float phaseBase = anisotropyGap * anisotropyGap +
                      2.0 * anisotropyMagnitude * (1.0 - alignedCosine);
    return (anisotropyGap * (1.0 + anisotropyMagnitude)) /
           (4.0 * VOLUME_PI * phaseBase * sqrt(phaseBase));
}

float calculateSampleJitter(vec2 pixelCoordinates) {
    return fract(sin(dot(floor(pixelCoordinates), JITTER_HASH_WEIGHTS)) * JITTER_HASH_SCALE);
}

float sampleScatteringWeight(vec3 worldPosition, vec3 viewDirection) {
    LightSample light = sampleLight(volume.light, worldPosition);
    if (light.attenuation <= 0.0 || !isVisibleToLight(worldPosition)) {
        return 0.0;
    }
    float phase = evaluateHenyeyGreensteinPhase(dot(viewDirection, light.L), volume.anisotropy);
    return light.attenuation * phase * transmittanceFromLight(worldPosition, light);
}

float integrateScattering(RaySegment ray, float stepLength) {
    float segmentOpticalDepth = volume.extinctionCoefficient * stepLength;
    float segmentTransmittance = exp(-segmentOpticalDepth);
    float sampleJitter = calculateSampleJitter(gl_FragCoord.xy);
    float transmittance = 1.0;
    float scattering = 0.0;
    for (int i = 0; i < volume.stepCount; ++i) {
        float distanceAlongRay = ray.startDistance + (float(i) + sampleJitter) * stepLength;
        vec3 samplePosition = ray.origin + ray.direction * distanceAlongRay;
        transmittance *= segmentTransmittance;
        if (volume.scatteringAlbedo > 0.0) {
            scattering += transmittance * segmentOpticalDepth *
                          sampleScatteringWeight(samplePosition, ray.direction);
        }
        if (transmittance < MIN_TRANSMITTANCE) {
            break;
        }
    }
    return scattering;
}

MediumIntegration integrateViewMedium(RaySegment ray) {
    if (!intersectMediumBounds(ray)) {
        return MediumIntegration(0.0, 1.0);
    }
    float pathLength = ray.endDistance - ray.startDistance;
    float stepLength = pathLength / float(volume.stepCount);
    float scattering = integrateScattering(ray, stepLength);
    // Keep full-path attenuation even when scattering integration stops early.
    float fullPathTransmittance = exp(-volume.extinctionCoefficient * pathLength);
    return MediumIntegration(scattering, fullPathTransmittance);
}

vec3 mediumScatteringColor(MediumIntegration medium) {
    return volume.light.colorAndType.rgb * (volume.scatteringAlbedo * medium.scattering);
}

#endif
