#version 450

// Ground-truth ambient occlusion: search both horizons in each view-space slice,
// then integrate the visible cosine-weighted arc of that slice.
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

vec3 viewPosFromDepth(vec2 uv, float depth) {
    vec4 view = ubo.invProj * vec4(uv * 2.0 - 1.0, depth, 1.0);
    return view.xyz / view.w;
}

float directionNoise(ivec2 pixel) {
    return float((((pixel.x + pixel.y) & 3) << 2) + (pixel.x & 3)) / 16.0;
}

float offsetNoise(ivec2 pixel) {
    return float((pixel.y - pixel.x) & 3) / 4.0;
}

vec2 packDepth(float depth) {
    float d = clamp(depth, 0.0, 1.0);
    float scaled = 256.0 * d;
    return vec2(floor(scaled) / 256.0, fract(scaled));
}

// Cosine-weighted visibility of one side of a slice (h measured from the view ray).
float integrateArc(float h, float n) {
    return (cos(n) + 2.0 * h * sin(n) - cos(2.0 * h - n)) * 0.25;
}

// Match Filament's non-bitmask horizon search: near samples establish a
// horizon; distant samples fade out, and lower samples use the thickness
// heuristic to avoid treating every depth discontinuity as a solid wall.
float updateHorizon(vec3 delta, vec3 viewDir, float horizonCos) {
    float distSq = dot(delta, delta);
    if (distSq < 1e-8) {
        return horizonCos;
    }
    float falloff = clamp(2.0 * distSq / (ubo.radius * ubo.radius), 0.0, 1.0);
    float sampleCos = dot(delta, viewDir) * inversesqrt(distSq);
    return sampleCos > horizonCos
            ? mix(sampleCos, horizonCos, falloff)
            : mix(horizonCos, sampleCos, ubo.thicknessHeuristic);
}

void main() {
    float depth = texture(depthSampler, inUV).r;
    if (depth >= 0.9999) {
        outAO = vec4(1.0);
        return;
    }

    float linearDepth = (ubo.nearPlane * ubo.farPlane) /
                        (ubo.farPlane - depth * (ubo.farPlane - ubo.nearPlane)) / ubo.farPlane;
    vec2 packedDepth = packDepth(linearDepth);
    vec3 origin = viewPosFromDepth(inUV, depth);
    vec3 normal = normalize(texture(normalSampler, inUV).rgb * 2.0 - 1.0);
    vec3 viewDir = normalize(-origin);
    vec2 texSize = vec2(textureSize(depthSampler, 0));
    float radiusPixels = ubo.projScale * ubo.radius / -origin.z;
    if (radiusPixels < 1.0) {
        outAO = vec4(1.0, packedDepth, 1.0);
        return;
    }

    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float rotation = directionNoise(pixel);
    float stepOffset = offsetNoise(pixel);
    float stepRadius = radiusPixels / float(ubo.stepCount + 1);
    float visibility = 0.0;
    for (int direction = 0; direction < ubo.directionCount; ++direction) {
        // PI covers unique slices; each slice searches along both directions.
        float angle = PI * (float(direction) + rotation) / float(ubo.directionCount);
        vec2 screenDir = vec2(cos(angle), sin(angle));
        vec3 tangent = normalize(viewPosFromDepth(inUV + screenDir / texSize, depth) - origin);
        tangent = normalize(tangent - viewDir * dot(tangent, viewDir));
        vec3 sliceNormal = normalize(cross(viewDir, tangent));
        vec3 projectedNormal = normal - sliceNormal * dot(normal, sliceNormal);
        float projectedLength = length(projectedNormal);
        if (projectedLength < 1e-5) {
            continue;
        }
        float cosNormal = clamp(dot(projectedNormal, viewDir) / projectedLength, 0.0, 1.0);
        float n = sign(dot(tangent, projectedNormal)) * acos(cosNormal);

        vec2 horizonCos = vec2(-1.0);
        for (int step = 0; step < ubo.stepCount; ++step) {
            // Uniform steps, with a one-pixel minimum to avoid sampling the origin.
            float samplePixels = max((float(step) + stepOffset) * stepRadius, 1.0 + float(step));
            vec2 uvOffset = samplePixels * screenDir / texSize;
            for (int side = 0; side < 2; ++side) {
                vec2 sampleUV = inUV + (side == 0 ? uvOffset : -uvOffset);
                if (any(lessThan(sampleUV, vec2(0.0))) ||
                    any(greaterThan(sampleUV, vec2(1.0)))) {
                    continue;
                }
                float sampleDepth = texture(depthSampler, sampleUV).r;
                if (sampleDepth >= 0.9999) {
                    continue;
                }
                vec3 delta = viewPosFromDepth(sampleUV, sampleDepth) - origin;
                horizonCos[side] = updateHorizon(delta, viewDir, horizonCos[side]);
            }
        }

        float h0 = -acos(clamp(horizonCos.y, -1.0, 1.0));
        float h1 = acos(clamp(horizonCos.x, -1.0, 1.0));
        h0 = n + clamp(h0 - n, -HALF_PI, HALF_PI);
        h1 = n + clamp(h1 - n, -HALF_PI, HALF_PI);
        visibility += projectedLength * (integrateArc(h0, n) + integrateArc(h1, n));
    }

    float ao = pow(clamp(visibility / float(ubo.directionCount), 0.0, 1.0), ubo.power);
    ao = mix(1.0, ao, ubo.intensity);
    outAO = vec4(ao, packedDepth, 1.0);
}
