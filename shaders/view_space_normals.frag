#version 450
#extension GL_GOOGLE_include_directive : require
#include "material_surface.glsl"

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 normalMatrix;
    vec4 baseColor;
    vec4 cameraPos;
    vec4 pbrParams;
} ubo;

layout(location = 2) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 0) in vec4 fragColor;

layout(location = 0) out vec4 outNormal;

void main() {
    applyAlphaMask(materialOpacity(fragTexCoord, fragColor.a));
    vec3 viewN = normalize(mat3(ubo.view) * fragNormal);
    outNormal = vec4(viewN * 0.5 + 0.5, 1.0);
}
