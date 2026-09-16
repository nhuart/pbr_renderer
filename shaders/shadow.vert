#version 450

layout(binding = 0) uniform ShadowUBO {
    mat4 lightSpaceTransform;
} ubo;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = ubo.lightSpaceTransform * vec4(inPosition, 1.0);
}
