#version 450

layout(binding = 1) uniform samplerCube skybox;

layout(location = 0) in vec3 fragDir;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = textureLod(skybox, fragDir, 0.0).rgb;
    
    // Reinhard tone mapping + gamma correction
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
