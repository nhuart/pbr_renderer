#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 normalMatrix;
    vec4 baseColor;
    vec4 cameraPos;
    vec4 pbrParams; // x=metallic, y=roughness, z=ambientIntensity
} ubo;

layout(binding = 1) uniform sampler2D texSampler;

struct GpuLight {
    vec4 colorAndType;        // xyz=RGB intensity, w=type (1=directional,2=spot,3=point)
    vec4 positionAndInvRange; // xyz=world position, w=1/range (spot/point)
    vec4 direction;           // xyz=normalized direction (directional/spot), w=unused
    vec4 coneScaleOffset;     // x=scale, y=offset for cone attenuation (spot only)
};

layout(binding = 2) uniform LightUBO {
    uvec4 counts; // x = number of active lights
    GpuLight lights[8];
} lights;

#ifdef USE_IBL
layout(binding = 3) uniform samplerCube irradianceMap;
layout(binding = 4) uniform samplerCube prefilterMap;
layout(binding = 5) uniform sampler2D   brdfLut;
#endif

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec4 fragBaseColor;
layout(location = 4) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float windowedFalloff(float distSq, float invRange) {
    float factor = distSq * invRange * invRange;
    float smoothF = max(1.0 - factor * factor, 0.0);
    return (smoothF * smoothF) / max(distSq, 1e-4);
}

struct LightSample {
    vec3 L;
    float attenuation;
};

LightSample sampleLight(GpuLight light) {
    int lightType = int(light.colorAndType.w);

    if (lightType == 1) {
        return LightSample(light.direction.xyz, 1.0);
    }

    vec3 toLight = light.positionAndInvRange.xyz - fragWorldPos;
    float distSq = dot(toLight, toLight);
    vec3 L = toLight / sqrt(distSq);
    float attenuation = windowedFalloff(distSq, light.positionAndInvRange.w);

    if (lightType == 2) {
        float cosTheta = dot(-L, light.direction.xyz);
        float cone = clamp(cosTheta * light.coneScaleOffset.x + light.coneScaleOffset.y, 0.0, 1.0);
        attenuation *= cone * cone;
    }

    return LightSample(L, attenuation);
}

vec3 lightContribution(GpuLight light, vec3 N, vec3 V, float NdotV, vec3 albedo, vec3 F0,
        float metallic, float roughness) {
    LightSample ls = sampleLight(light);

    vec3 H = normalize(V + ls.L);
    float NdotL = max(dot(N, ls.L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3  F = fresnelSchlick(HdotV, F0);

    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.0001);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * light.colorAndType.xyz * ls.attenuation * NdotL;
}

#ifdef USE_IBL
// sampling a prefiltered/blurred environment — rough surfaces have scattered their
// specular lobe, so the Fresnel peak at grazing angles must be dampened to match
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 iblAmbient(vec3 N, vec3 V, float NdotV, vec3 albedo, vec3 F0, float metallic, float roughness) {
    vec3 kS = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse    = kD * irradiance * albedo;

    vec3 R = reflect(-V, N);
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLut, vec2(NdotV, roughness)).rg;
    vec3 specular = prefilteredColor * (kS * brdf.x + brdf.y);

    return diffuse + specular;
}
#endif

void main() {
    float metallic         = ubo.pbrParams.x;
    float roughness        = ubo.pbrParams.y;
    float ambientIntensity = ubo.pbrParams.z;

    vec4 texColor = texture(texSampler, fragTexCoord);
    vec3 albedo   = texColor.rgb * fragBaseColor.rgb;

    vec3 N = normalize(fragNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 V = normalize(ubo.cameraPos.xyz - fragWorldPos);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float NdotV = max(dot(N, V), 0.0001);

    vec3 Lo = vec3(0.0);
    int numLights = int(lights.counts.x);
    for (int i = 0; i < numLights; i++) {
        Lo += lightContribution(lights.lights[i], N, V, NdotV, albedo, F0, metallic, roughness);
    }

#ifdef USE_IBL
    vec3 ambient = iblAmbient(N, V, NdotV, albedo, F0, metallic, roughness);
#else
    vec3 ambient = ambientIntensity * albedo;
#endif

    vec3 color = ambient + Lo;

    // Reinhard tone mapping + gamma correction
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, texColor.a * fragBaseColor.a);
}
