#version 450

layout(binding = 0) uniform ShadowUBO {
    mat4 lightSpaceMVP;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec4 inTangent;

void main() {
    gl_Position = ubo.lightSpaceMVP * vec4(inPosition, 1.0);
}
