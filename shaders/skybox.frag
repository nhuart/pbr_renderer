#version 450

layout(binding = 1) uniform samplerCube prefilterMap;

layout(location = 0) in vec3 fragDir;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = textureLod(prefilterMap, fragDir, 0.0).rgb;

    outColor = vec4(color, 1.0);
}
