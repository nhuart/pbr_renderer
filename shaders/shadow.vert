#version 450

layout(binding = 0) uniform ShadowUBO {
    mat4 lightSpaceTransform;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out float fragVertexAlpha;

void main() {
    gl_Position = ubo.lightSpaceTransform * vec4(inPosition, 1.0);
    fragTexCoord = inTexCoord;
    fragVertexAlpha = inColor.a;
}
