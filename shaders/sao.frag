#version 450

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
layout(location = 0) out vec4 outAO; // R=AO, GB=packed linearized depth, A=unused

const float PI = 3.14159265359;

// Reconstruct view-space position from depth and UV.
vec3 viewPosFromDepth(vec2 uv, float depth) {
    vec4 normalizedDeviceCoordinate = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 view = ubo.invProj * normalizedDeviceCoordinate;
    return view.xyz / view.w;
}

float sampleDepth(vec2 uv) {
    return texture(depthSampler, uv).r;
}

// Linearize Vulkan [0,1] NDC depth to [0,1] over [near,far]
float linearizeDepth(float depth) {
    float near = ubo.nearPlane;
    float far  = ubo.farPlane;
    // Vulkan depth is already [0,1]: z_view = near*far / (far - depth*(far-near))
    return (near * far) / (far - depth * (far - near)) / far;
}

// Pack a normalized [0,1] linear depth into two 8-bit channels
vec2 packDepth(float d) {
    float depth = clamp(d, 0.0, 1.0);
    float scaled = 256.0 * depth;
    float highByte = floor(scaled) * (1.0 / 256.0);
    float lowByte = scaled - floor(scaled);
    return vec2(highByte, lowByte);
}

// Per-pixel rotation hash: maps pixel position to a [0,1) value that is
// spatially uncorrelated — each 8x8 block covers [0,1] uniformly with no
// visible structure. Used to rotate each pixel's sample spiral independently
// so the spiral pattern doesn't appear as a visible artifact in the AO output.
float interleavedGradientNoise(vec2 pos) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(pos, magic.xy)));
}

void main() {
    vec2 texSize = vec2(textureSize(depthSampler, 0));
    vec2 pixelPos = inUV * texSize;

    float depth = sampleDepth(inUV);
    float linearDepth = linearizeDepth(depth);
    vec2 packedDepth = packDepth(linearDepth);

    // Skip skybox / far-plane pixels
    if (depth >= 0.9999) {
        outAO = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec3 origin = viewPosFromDepth(inUV, depth);
    vec3 normal = texture(normalSampler, inUV).rgb * 2.0 - 1.0; // decode [0,1] → [−1,1]
    normal = normalize(normal);

    // Project world radius onto screen (in texels)
    float screenSpaceRadius = ubo.projScale * ubo.radius / -origin.z;
    if (screenSpaceRadius < 1.0) {
        outAO = vec4(1.0, packedDepth, 1.0);
        return;
    }

    float noise = interleavedGradientNoise(pixelPos);
    float rotation = (2.0 * PI * 2.4) * noise;  // golden-angle offset matching Filament
    float invSampleCount = 1.0 / (float(ubo.sampleCount) - 0.5);

    float invRadiusSq = 1.0 / (ubo.radius * ubo.radius);
    float peak = 0.1 * ubo.radius;
    float peakSq = peak * peak;
    float sampleIntensity = (2.0 * PI * peak) * ubo.intensity / float(ubo.sampleCount);

    float occlusionSum = 0.0;
    for (int i = 0; i < ubo.sampleCount; i++) {
        // Quadratic alpha: clusters taps near center like Filament
        float normalizedRadius = (float(i) + noise + 0.5) * invSampleCount;
        float angle = normalizedRadius * float(ubo.spiralTurns) * 2.0 * PI + rotation;

        vec2 unitDir = vec2(cos(angle), sin(angle));
        float sampleRadius = normalizedRadius * normalizedRadius * screenSpaceRadius;  // r² gives quadratic spacing
        vec2 sampleUV = inUV + sampleRadius * unitDir / texSize;

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
            sampleUV.y < 0.0 || sampleUV.y > 1.0) {
            continue;
        }

        float sampleRawDepth = sampleDepth(sampleUV);
        vec3 samplePos = viewPosFromDepth(sampleUV, sampleRawDepth);

        vec3 horizonVector = samplePos - origin;
        float distSq = dot(horizonVector, horizonVector);
        float normalDot = dot(horizonVector, normal);

        // Smooth quadratic falloff — zero at radius boundary
        float weight = max(0.0, 1.0 - distSq * invRadiusSq);
        weight = weight * weight;

        float sampleOcclusion = max(0.0, normalDot + origin.z * ubo.bias) / (distSq + peakSq);
        occlusionSum += weight * sampleOcclusion;
    }

    float occlusion = sqrt(occlusionSum * sampleIntensity);
    occlusion = clamp(1.0 - occlusion, 0.0, 1.0);
    occlusion = pow(occlusion, ubo.power * 2.0);
    outAO = vec4(occlusion, packedDepth, 1.0);
}
