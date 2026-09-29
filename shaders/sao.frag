#version 450

// SAO samples a rotating screen-space spiral to estimate local obscurance.
layout(binding = 0) uniform SaoUBO {
    mat4  proj;
    mat4  invProj;
    float radius;
    float bias;
    float power;
    float intensity;
    float projScale;
    int   sampleCount;
    int   spiralTurns;
    float nearPlane;
    float farPlane;
    float pad[2];
} ubo;

layout(binding = 1) uniform sampler2D depthSampler;
layout(binding = 2) uniform sampler2D normalSampler;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outAO; // R=AO, GB=packed linear depth for bilateral blur

const float PI = 3.14159265359;

vec3 reconstructViewPosition(vec2 screenUV, float deviceDepth) {
    vec4 clipPosition = vec4(screenUV * 2.0 - 1.0, deviceDepth, 1.0);
    vec4 viewPosition = ubo.invProj * clipPosition;
    return viewPosition.xyz / viewPosition.w;
}

float readDeviceDepth(vec2 screenUV) {
    return texture(depthSampler, screenUV).r;
}

// Normalize linear view depth to [0, 1] for depth-aware blurring.
float normalizedLinearDepth(float deviceDepth) {
    float nearPlane = ubo.nearPlane;
    float farPlane = ubo.farPlane;
    return (nearPlane * farPlane) /
           (farPlane - deviceDepth * (farPlane - nearPlane)) / farPlane;
}

// Pack normalized linear depth into the AO texture's two 8-bit channels.
vec2 packLinearDepth(float linearDepth) {
    float clampedDepth = clamp(linearDepth, 0.0, 1.0);
    float scaledDepth = 256.0 * clampedDepth;
    float highByte = floor(scaledDepth) * (1.0 / 256.0);
    float lowByte = scaledDepth - floor(scaledDepth);
    return vec2(highByte, lowByte);
}

// Rotate the sampling spiral per pixel to break up visible spiral patterns.
float interleavedGradientNoise(vec2 pixelCoord) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(pixelCoord, magic.xy)));
}

void main() {
    // Reconstruct the visible surface and preserve depth for the bilateral blur.
    vec2 depthTextureSize = vec2(textureSize(depthSampler, 0));
    vec2 pixelCoord = inUV * depthTextureSize;

    float centerDeviceDepth = readDeviceDepth(inUV);
    float centerLinearDepth = normalizedLinearDepth(centerDeviceDepth);
    vec2 packedDepth = packLinearDepth(centerLinearDepth);

    if (centerDeviceDepth >= 0.9999) {
        outAO = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec3 centerPosition = reconstructViewPosition(inUV, centerDeviceDepth);
    vec3 viewNormal = texture(normalSampler, inUV).rgb * 2.0 - 1.0;
    viewNormal = normalize(viewNormal);

    // Convert the view-space AO radius to pixels at this surface's depth.
    float screenRadiusPixels = ubo.projScale * ubo.radius / -centerPosition.z;
    if (screenRadiusPixels < 1.0) {
        outAO = vec4(1.0, packedDepth, 1.0);
        return;
    }

    float sampleJitter = interleavedGradientNoise(pixelCoord);
    float spiralRotation = (2.0 * PI * 2.4) * sampleJitter;
    float inverseSampleCount = 1.0 / (float(ubo.sampleCount) - 0.5);

    float inverseRadiusSquared = 1.0 / (ubo.radius * ubo.radius);
    float softeningRadius = 0.1 * ubo.radius;
    float softeningRadiusSquared = softeningRadius * softeningRadius;
    float occlusionScale = (2.0 * PI * softeningRadius) * ubo.intensity /
                           float(ubo.sampleCount);

    // Sample a spiral with quadratic spacing: more taps near the center pixel.
    float weightedOcclusionSum = 0.0;
    for (int sampleIndex = 0; sampleIndex < ubo.sampleCount; sampleIndex++) {
        float sampleFraction = (float(sampleIndex) + sampleJitter + 0.5) * inverseSampleCount;
        float spiralAngle = sampleFraction * float(ubo.spiralTurns) * 2.0 * PI +
                            spiralRotation;

        vec2 sampleDirection = vec2(cos(spiralAngle), sin(spiralAngle));
        float sampleDistancePixels = sampleFraction * sampleFraction * screenRadiusPixels;
        vec2 sampleUV = inUV + sampleDistancePixels * sampleDirection / depthTextureSize;

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
            sampleUV.y < 0.0 || sampleUV.y > 1.0) {
            continue;
        }

        float sampleDeviceDepth = readDeviceDepth(sampleUV);
        vec3 samplePosition = reconstructViewPosition(sampleUV, sampleDeviceDepth);

        vec3 toSample = samplePosition - centerPosition;
        float distanceSquared = dot(toSample, toSample);
        float normalComponent = dot(toSample, viewNormal);

        // Distant samples contribute less; the bias suppresses self-occlusion.
        float distanceWeight = max(0.0, 1.0 - distanceSquared * inverseRadiusSquared);
        distanceWeight = distanceWeight * distanceWeight;

        float sampleContribution = max(0.0, normalComponent + centerPosition.z * ubo.bias) /
                                   (distanceSquared + softeningRadiusSquared);
        weightedOcclusionSum += distanceWeight * sampleContribution;
    }

    // Convert accumulated obscurance to visibility (1 = unoccluded).
    float obscurance = sqrt(weightedOcclusionSum * occlusionScale);
    float aoVisibility = clamp(1.0 - obscurance, 0.0, 1.0);
    aoVisibility = pow(aoVisibility, ubo.power * 2.0);
    outAO = vec4(aoVisibility, packedDepth, 1.0);
}
