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
    int   maxLevel;
    float pad;
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

float readDeviceDepth(vec2 screenUV, float mipLevel) {
    return textureLod(depthSampler, screenUV, mipLevel).r;
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

struct SaoContext {
    vec2 centerUV;
    vec2 depthTextureSize;
    vec3 centerPosition;
    vec3 viewNormal;
    float screenRadiusPixels;
    float inverseRadiusSquared;
    float softeningRadiusSquared;
};

struct SpiralPattern {
    float jitter;
    float rotation;
    float inverseSampleCount;
};

float sampleObscurance(vec2 sampleUV, float mipLevel, SaoContext context) {
    float sampleDeviceDepth = readDeviceDepth(sampleUV, mipLevel);
    vec3 samplePosition = reconstructViewPosition(sampleUV, sampleDeviceDepth);
    vec3 toSample = samplePosition - context.centerPosition;
    float distanceSquared = dot(toSample, toSample);
    float normalComponent = dot(toSample, context.viewNormal);

    // Distant samples contribute less; the bias suppresses self-occlusion.
    float distanceWeight = max(0.0, 1.0 - distanceSquared * context.inverseRadiusSquared);
    distanceWeight *= distanceWeight;
    float sampleContribution = max(0.0, normalComponent + context.centerPosition.z * ubo.bias) /
                               (distanceSquared + context.softeningRadiusSquared);
    return distanceWeight * sampleContribution;
}

float spiralSampleObscurance(int sampleIndex, SpiralPattern spiral, SaoContext context) {
    // Quadratic spacing puts more taps near the center; only the radius is jittered.
    float sampleFraction = (float(sampleIndex) + spiral.jitter + 0.5) * spiral.inverseSampleCount;
    float spiralAngle = float(sampleIndex) * spiral.inverseSampleCount * float(ubo.spiralTurns) *
                        2.0 * PI + spiral.rotation;

    vec2 sampleDirection = vec2(cos(spiralAngle), sin(spiralAngle));
    float sampleDistancePixels = max(1.0, sampleFraction * sampleFraction * context.screenRadiusPixels);
    vec2 sampleUV = context.centerUV + sampleDistancePixels * sampleDirection / context.depthTextureSize;

    if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
        sampleUV.y < 0.0 || sampleUV.y > 1.0) {
        return 0.0;
    }

    // Distant taps read coarser depth, matching Filament's structure pass.
    float mipLevel = clamp(floor(log2(sampleDistancePixels)) - 3.0, 0.0,
                           float(ubo.maxLevel));
    return sampleObscurance(sampleUV, mipLevel, context);
}

float accumulateObscurance(SaoContext context) {
    float jitter = interleavedGradientNoise(context.centerUV * context.depthTextureSize);
    SpiralPattern spiral = SpiralPattern(jitter, (2.0 * PI * 2.4) * jitter,
            1.0 / (float(ubo.sampleCount) - 0.5));
    float weightedOcclusionSum = 0.0;
    for (int sampleIndex = 0; sampleIndex < ubo.sampleCount; ++sampleIndex) {
        weightedOcclusionSum += spiralSampleObscurance(sampleIndex, spiral, context);
    }
    return weightedOcclusionSum;
}

float obscuranceToVisibility(float weightedOcclusionSum) {
    float softeningRadius = 0.1 * ubo.radius;
    float occlusionScale = (2.0 * PI * softeningRadius) * ubo.intensity /
                           float(ubo.sampleCount);
    float obscurance = sqrt(weightedOcclusionSum * occlusionScale);
    float aoVisibility = clamp(1.0 - obscurance, 0.0, 1.0);
    return pow(aoVisibility, ubo.power * 2.0);
}

void main() {
    vec2 depthTextureSize = vec2(textureSize(depthSampler, 0));
    float centerDeviceDepth = readDeviceDepth(inUV, 0.0);
    vec2 packedDepth = packLinearDepth(normalizedLinearDepth(centerDeviceDepth));
    if (centerDeviceDepth >= 0.9999) {
        outAO = vec4(1.0);
        return;
    }

    vec3 centerPosition = reconstructViewPosition(inUV, centerDeviceDepth);
    vec3 viewNormal = normalize(texture(normalSampler, inUV).rgb * 2.0 - 1.0);
    float screenRadiusPixels = ubo.projScale * ubo.radius / -centerPosition.z;
    if (screenRadiusPixels < 1.0) {
        outAO = vec4(1.0, packedDepth, 1.0);
        return;
    }

    float softeningRadius = 0.1 * ubo.radius;
    SaoContext context = SaoContext(inUV, depthTextureSize, centerPosition, viewNormal,
            screenRadiusPixels, 1.0 / (ubo.radius * ubo.radius), softeningRadius * softeningRadius);
    outAO = vec4(obscuranceToVisibility(accumulateObscurance(context)), packedDepth, 1.0);
}
