#version 450

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec4 fragBaseColor;
layout(location = 4) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

const float ambient = 0.15;

void main() {
    vec3 lightDir = normalize(vec3(1.0, 2.0, 1.0));
    float diffuse = max(dot(normalize(fragNormal), lightDir), 0.0);
    float light = ambient + diffuse;

    vec4 texColor = texture(texSampler, fragTexCoord);
    vec3 baseColor = texColor.rgb * fragBaseColor.rgb;
    outColor = vec4(baseColor * light, texColor.a * fragBaseColor.a);
}
