#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 normalMatrix;
    vec4 baseColor;
    vec4 cameraPos;
    vec4 pbrParams; // x=metallic, y=roughness
} ubo;

layout(binding = 1) uniform sampler2D texSampler;

struct GpuLight {
    vec4 colorAndType;     // xyz=RGB intensity, w=type (1=directional,2=spot,3=point)
    vec4 positionAndRange; // xyz=position, w=invRange (spot/point)
    vec4 directionAndCone; // xyz=direction
    vec4 coneParams;       // x=scale, y=offset (spot only)
};

layout(binding = 2) uniform LightUBO {
    uvec4 counts; // x = number of active lights
    GpuLight lights[8];
} lights;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec4 fragBaseColor;
layout(location = 4) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

// GGX / Trowbridge-Reitz normal distribution function
float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Smith's method with GGX geometry function (Schlick approximation)
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Windowed inverse-square falloff: (1-(d/r)^4)^2 / max(d^2, 1e-4)
float windowedFalloff(float distSq, float invRange) {
    float factor = distSq * invRange * invRange;
    float smoothF = max(1.0 - factor * factor, 0.0);
    return (smoothF * smoothF) / max(distSq, 1e-4);
}

void main() {
    float metallic  = ubo.pbrParams.x;
    float roughness = ubo.pbrParams.y;

    vec4 texColor   = texture(texSampler, fragTexCoord);
    vec3 albedo     = texColor.rgb * fragBaseColor.rgb;

    vec3 N = normalize(fragNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 V = normalize(ubo.cameraPos.xyz - fragWorldPos);

    // Dialectric F0 = 0.04; metals use albedo as F0
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float NdotV = max(dot(N, V), 0.0001);

    vec3 Lo = vec3(0.0);

    int numLights = int(lights.counts.x);
    for (int i = 0; i < numLights; i++) {
        GpuLight light = lights.lights[i];
        int lightType = int(light.colorAndType.w);
        vec3 lightColor = light.colorAndType.xyz;

        vec3 L = vec3(0.0);
        float attenuation = 1.0;

        if (lightType == 1) {
            L = light.directionAndCone.xyz;
        } else if (lightType == 2) {
            vec3 toLight = light.positionAndRange.xyz - fragWorldPos;
            float distSq = dot(toLight, toLight);
            L = toLight / sqrt(distSq);
            attenuation = windowedFalloff(distSq, light.positionAndRange.w);
            float cosTheta = dot(-L, light.directionAndCone.xyz);
            float cone = clamp(cosTheta * light.coneParams.x + light.coneParams.y, 0.0, 1.0);
            attenuation *= cone * cone;
        } else if (lightType == 3) {
            vec3 toLight = light.positionAndRange.xyz - fragWorldPos;
            float distSq = dot(toLight, toLight);
            L = toLight / sqrt(distSq);
            attenuation = windowedFalloff(distSq, light.positionAndRange.w);
        }

        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float HdotV = max(dot(H, V), 0.0);

        // Cook-Torrance specular BRDF
        float D = distributionGGX(NdotH, roughness);
        float G = geometrySmith(NdotV, NdotL, roughness);
        vec3  F = fresnelSchlick(HdotV, F0);

        vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.0001);

        // Energy-conserving diffuse: metals have no diffuse
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        vec3 diffuse = kD * albedo / PI;

        Lo += (diffuse + specular) * lightColor * attenuation * NdotL;
    }

    // Ambient approximation (no IBL yet)
    vec3 ambient = vec3(0.03) * albedo;

    vec3 color = ambient + Lo;

    // Reinhard tone mapping + gamma correction
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, texColor.a * fragBaseColor.a);
}
