#version 450
#extension GL_GOOGLE_include_directive : require

#include "volumetric_medium.glsl"

layout(binding = 1) uniform sampler2D sceneDepth;
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 worldPosition = volume.invViewProj * vec4(uv * 2.0 - 1.0, depth, 1.0);
    return worldPosition.xyz / worldPosition.w;
}

RaySegment createViewRay() {
    vec3 origin = volume.cameraPos.xyz;
    vec3 direction = normalize(reconstructWorldPosition(inUV, FAR_PLANE_DEPTH) - origin);
    float surfaceDepth = texture(sceneDepth, inUV).r;
    float endDistance = volume.maxDistance;
    if (surfaceDepth < FAR_PLANE_DEPTH) {
        float surfaceDistance = length(reconstructWorldPosition(inUV, surfaceDepth) - origin);
        endDistance = min(endDistance, surfaceDistance);
    }
    return RaySegment(origin, direction, 0.0, endDistance);
}

void main() {
    MediumIntegration medium = integrateViewMedium(createViewRay());
    outColor = vec4(mediumScatteringColor(medium), medium.transmittance);
}
