#version 450

// GTAO estimates ambient visibility by searching both sides of view-space slices.
layout(binding = 0) uniform GtaoUBO {
    mat4  proj;
    mat4  invProj;
    float radius;
    float thicknessHeuristic;
    float power;
    float intensity;
    float projScale;
    int   stepCount;
    int   directionCount;
    float nearPlane;
    float farPlane;
    float pad[2];
} ubo;

layout(binding = 1) uniform sampler2D depthSampler;
layout(binding = 2) uniform sampler2D normalSampler;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outAO; // R=AO, GB=packed linear depth for the shared bilateral blur

const float PI = 3.14159265359;
const float HALF_PI = 0.5 * PI;

vec3 reconstructViewPosition(vec2 screenUV, float deviceDepth) {
    vec4 viewPosition = ubo.invProj * vec4(screenUV * 2.0 - 1.0, deviceDepth, 1.0);
    return viewPosition.xyz / viewPosition.w;
}

// Per-pixel offsets distribute slice directions and sample steps over a 4x4 tile.
float sliceRotationNoise(ivec2 pixelCoord) {
    return float((((pixelCoord.x + pixelCoord.y) & 3) << 2) + (pixelCoord.x & 3)) / 16.0;
}

float stepJitterNoise(ivec2 pixelCoord) {
    return float((pixelCoord.y - pixelCoord.x) & 3) / 4.0;
}

vec2 packLinearDepth(float linearDepth) {
    float clampedDepth = clamp(linearDepth, 0.0, 1.0);
    float scaledDepth = 256.0 * clampedDepth;
    return vec2(floor(scaledDepth) / 256.0, fract(scaledDepth));
}

// Integrate cosine-weighted visibility up to a horizon angle in one slice.
float integrateVisibleArc(float horizonAngle, float normalAngle) {
    return (cos(normalAngle) + 2.0 * horizonAngle * sin(normalAngle) -
            cos(2.0 * horizonAngle - normalAngle)) * 0.25;
}

// Raise the horizon with nearby occluders; relax it slightly for lower samples.
float updateHorizonCosine(vec3 toSample, vec3 towardCamera, float currentHorizonCosine) {
    float distanceSquared = dot(toSample, toSample);
    if (distanceSquared < 1e-8) {
        return currentHorizonCosine;
    }
    float falloff = clamp(2.0 * distanceSquared / (ubo.radius * ubo.radius), 0.0, 1.0);
    float sampleHorizonCosine = dot(toSample, towardCamera) * inversesqrt(distanceSquared);
    return sampleHorizonCosine > currentHorizonCosine
            ? mix(sampleHorizonCosine, currentHorizonCosine, falloff)
            : mix(currentHorizonCosine, sampleHorizonCosine, ubo.thicknessHeuristic);
}

struct HorizonSearch {
    vec2 centerUV;
    vec3 centerPosition;
    vec3 towardCamera;
    vec2 depthTextureSize;
    float pixelsPerStep;
    float stepJitter;
};

float sampleHorizonCosine(vec2 sampleUV, HorizonSearch search, float currentHorizonCosine) {
    if (any(lessThan(sampleUV, vec2(0.0))) ||
        any(greaterThan(sampleUV, vec2(1.0)))) {
        return currentHorizonCosine;
    }
    float sampleDeviceDepth = texture(depthSampler, sampleUV).r;
    if (sampleDeviceDepth >= 0.9999) {
        return currentHorizonCosine;
    }
    vec3 toSample = reconstructViewPosition(sampleUV, sampleDeviceDepth) - search.centerPosition;
    return updateHorizonCosine(toSample, search.towardCamera, currentHorizonCosine);
}

vec2 findHorizonCosines(vec2 screenDirection, HorizonSearch search) {
    vec2 horizonCosines = vec2(-1.0);
    for (int stepIndex = 0; stepIndex < ubo.stepCount; ++stepIndex) {
        float sampleDistancePixels = max((float(stepIndex) + search.stepJitter) * search.pixelsPerStep,
                                         1.0 + float(stepIndex));
        vec2 sampleOffsetUV = sampleDistancePixels * screenDirection / search.depthTextureSize;
        for (int sideIndex = 0; sideIndex < 2; ++sideIndex) {
            vec2 sampleUV = search.centerUV + (sideIndex == 0 ? sampleOffsetUV : -sampleOffsetUV);
            horizonCosines[sideIndex] = sampleHorizonCosine(
                    sampleUV, search, horizonCosines[sideIndex]);
        }
    }
    return horizonCosines;
}

float integrateSliceHorizons(vec2 horizonCosines, float normalAngle) {
    // Limit the horizons to the surface hemisphere before integrating visible light.
    float negativeHorizonAngle = -acos(clamp(horizonCosines.y, -1.0, 1.0));
    float positiveHorizonAngle = acos(clamp(horizonCosines.x, -1.0, 1.0));
    negativeHorizonAngle = normalAngle +
            clamp(negativeHorizonAngle - normalAngle, -HALF_PI, HALF_PI);
    positiveHorizonAngle = normalAngle +
            clamp(positiveHorizonAngle - normalAngle, -HALF_PI, HALF_PI);
    return integrateVisibleArc(negativeHorizonAngle, normalAngle) +
           integrateVisibleArc(positiveHorizonAngle, normalAngle);
}

float sliceVisibility(vec2 screenDirection, vec3 viewNormal, HorizonSearch search,
        float centerDeviceDepth) {
    // Project the surface normal into the plane containing this slice and the view ray.
    vec3 sliceTangent = normalize(reconstructViewPosition(
            search.centerUV + screenDirection / search.depthTextureSize, centerDeviceDepth) -
            search.centerPosition);
    sliceTangent = normalize(sliceTangent - search.towardCamera * dot(sliceTangent, search.towardCamera));
    vec3 slicePlaneNormal = normalize(cross(search.towardCamera, sliceTangent));
    vec3 normalInSlice = viewNormal - slicePlaneNormal * dot(viewNormal, slicePlaneNormal);
    float normalProjectionLength = length(normalInSlice);
    if (normalProjectionLength < 1e-5) {
        return 0.0;
    }
    float normalViewCosine = clamp(dot(normalInSlice, search.towardCamera) /
                                   normalProjectionLength, 0.0, 1.0);
    float normalAngle = sign(dot(sliceTangent, normalInSlice)) * acos(normalViewCosine);

    vec2 horizonCosines = findHorizonCosines(screenDirection, search);
    return normalProjectionLength * integrateSliceHorizons(horizonCosines, normalAngle);
}

float accumulateSliceVisibility(vec3 viewNormal, HorizonSearch search, float centerDeviceDepth,
        ivec2 pixelCoord) {
    float sliceRotation = sliceRotationNoise(pixelCoord);
    float sliceVisibilitySum = 0.0;
    // A half-turn covers all unique slices because each is sampled in both directions.
    for (int sliceIndex = 0; sliceIndex < ubo.directionCount; ++sliceIndex) {
        float sliceAngle = PI * (float(sliceIndex) + sliceRotation) / float(ubo.directionCount);
        vec2 screenDirection = vec2(cos(sliceAngle), sin(sliceAngle));
        sliceVisibilitySum += sliceVisibility(screenDirection, viewNormal, search, centerDeviceDepth);
    }
    return sliceVisibilitySum;
}

void main() {
    // Reconstruct the visible surface; sky pixels cannot receive occlusion.
    float centerDeviceDepth = texture(depthSampler, inUV).r;
    if (centerDeviceDepth >= 0.9999) {
        outAO = vec4(1.0);
        return;
    }

    float linearDepth = (ubo.nearPlane * ubo.farPlane) /
                        (ubo.farPlane - centerDeviceDepth * (ubo.farPlane - ubo.nearPlane)) /
                        ubo.farPlane;
    vec2 packedDepth = packLinearDepth(linearDepth);
    vec3 centerPosition = reconstructViewPosition(inUV, centerDeviceDepth);
    vec3 viewNormal = normalize(texture(normalSampler, inUV).rgb * 2.0 - 1.0);
    vec3 towardCamera = normalize(-centerPosition);
    vec2 depthTextureSize = vec2(textureSize(depthSampler, 0));
    float screenRadiusPixels = ubo.projScale * ubo.radius / -centerPosition.z;
    if (screenRadiusPixels < 1.0) {
        outAO = vec4(1.0, packedDepth, 1.0);
        return;
    }

    ivec2 pixelCoord = ivec2(gl_FragCoord.xy);
    HorizonSearch search = HorizonSearch(inUV, centerPosition, towardCamera, depthTextureSize,
            screenRadiusPixels / float(ubo.stepCount + 1), stepJitterNoise(pixelCoord));
    float sliceVisibilitySum = accumulateSliceVisibility(viewNormal, search, centerDeviceDepth, pixelCoord);

    // Average slices, adjust contrast, then keep depth for the bilateral blur.
    float aoVisibility = pow(clamp(sliceVisibilitySum / float(ubo.directionCount), 0.0, 1.0),
                             ubo.power);
    aoVisibility = mix(1.0, aoVisibility, ubo.intensity);
    outAO = vec4(aoVisibility, packedDepth, 1.0);
}
