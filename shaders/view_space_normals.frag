#version 450

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

layout(location = 0) out vec4 outNormal;

void main() {
    vec3 viewN = normalize(mat3(ubo.view) * fragNormal);
    outNormal = vec4(viewN * 0.5 + 0.5, 1.0);
}
