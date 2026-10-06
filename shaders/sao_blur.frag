#version 450

// Bilateral separable blur shared by SAO and GTAO.
// Run twice: horizontal (passIndex=0) then vertical (passIndex=1).
// Depth is packed in the GB channels of the AO texture (no separate depth sampler).

layout(binding = 0) uniform BlurUBO {
    int   passIndex;
    float farPlaneOverEdgeDistance; // -far / bilateralThreshold
    int   kernelRadius; // number of taps on each side, in units of sampleStride
    int   sampleStride; // SAO medium blur: 2 pixels/tap; GTAO high blur: 1
} ubo;

layout(binding = 1) uniform sampler2D aoSampler; // R=AO, GB=packed linearized depth

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outAO;

// Low-discrepancy noise for dithering (same hash used in sao.frag)
float interleavedGradientNoise(vec2 pos) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(pos, magic.xy)));
}

// Unpack GB channels back to a normalized [0,1] linear depth value
float unpackDepth(vec2 gb) {
    return (gb.x * (256.0 / 257.0) + gb.y * (1.0 / 257.0));
}

// Gaussian weight with sigma=6 in screen pixels.
float gaussianWeight(int i) {
    float x = float(i * ubo.sampleStride);
    return exp(-(x * x) / 72.0); // 2 * sigma^2 = 2 * 36 = 72
}

// Smooth quadratic bilateral weight matching Filament's bilateralWeight()
float bilateralWeight(float centerDepth, float sampleDepth) {
    float diff = (sampleDepth - centerDepth) * ubo.farPlaneOverEdgeDistance;
    return max(0.0, 1.0 - diff * diff);
}

float blurAmbientOcclusion(vec2 centerUV, vec3 center, vec2 axis) {
    float centerDepth = unpackDepth(center.gb);
    float totalWeight = gaussianWeight(0);
    float aoSum = center.r * totalWeight;

    for (int tap = 1; tap <= ubo.kernelRadius; ++tap) {
        float gaussian = gaussianWeight(tap);
        vec2 offset = float(tap * ubo.sampleStride) * axis;

        vec3 neighborFwd = texture(aoSampler, clamp(centerUV + offset, vec2(0.0), vec2(1.0))).rgb;
        vec3 neighborBwd = texture(aoSampler, clamp(centerUV - offset, vec2(0.0), vec2(1.0))).rgb;

        float weightFwd = gaussian * bilateralWeight(centerDepth, unpackDepth(neighborFwd.gb));
        float weightBwd = gaussian * bilateralWeight(centerDepth, unpackDepth(neighborBwd.gb));

        aoSum += neighborFwd.r * weightFwd + neighborBwd.r * weightBwd;
        totalWeight += weightFwd + weightBwd;
    }
    return aoSum / totalWeight;
}

void main() {
    vec3 center = texture(aoSampler, inUV).rgb;
    // Skip skybox pixels written as (1,1,1,1).
    if (center.g * center.b >= 0.9999) {
        outAO = vec4(center, 1.0);
        return;
    }

    vec2 texelSize = 1.0 / vec2(textureSize(aoSampler, 0));
    vec2 axis = (ubo.passIndex == 0) ? vec2(texelSize.x, 0.0) : vec2(0.0, texelSize.y);
    float ao = blurAmbientOcclusion(inUV, center, axis);

    // Dithering to break up 8-bit quantization banding (matches Filament)
    ao += (interleavedGradientNoise(gl_FragCoord.xy) - 0.5) / 255.0;

    outAO = vec4(ao, center.gb, 1.0);
}
