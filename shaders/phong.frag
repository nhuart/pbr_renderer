#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 normalMatrix;
    vec4 baseColor;
    vec4 cameraPos;
} ubo;

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec4 fragBaseColor;
layout(location = 4) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

const float ambient = 0.15;
const float shininess = 32.0;
const float specularStrength = 0.5;

void main() {
    vec3 lightDir = normalize(vec3(1.0, 2.0, 1.0));
    vec3 normal = normalize(fragNormal);
    if (!gl_FrontFacing) normal = -normal;
    vec3 viewDir = normalize(ubo.cameraPos.xyz - fragWorldPos);
    float diffuse = max(dot(normal, lightDir), 0.0);

    vec3 reflectDir = reflect(-lightDir, normal);
    float specular = specularStrength * pow(max(dot(viewDir, reflectDir), 0.0), shininess);

    vec4 texColor = texture(texSampler, fragTexCoord);
    vec3 baseColor = texColor.rgb * fragBaseColor.rgb;
    float light = ambient + diffuse + specular;
    outColor = vec4(baseColor * light, texColor.a * fragBaseColor.a);
}
