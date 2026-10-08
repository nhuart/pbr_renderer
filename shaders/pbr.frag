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
layout(binding = 3) uniform IblSHUBO {
    vec4 sh[9];
} iblSH;
layout(binding = 4) uniform samplerCube prefilterMap;
layout(binding = 5) uniform sampler2D   brdfLut;
#endif

#ifdef USE_NORMAL_MAP
layout(binding = 6) uniform sampler2D normalMapSampler;
#endif

#ifdef USE_SHADOW
layout(binding = 7) uniform sampler2D shadowMap;
layout(binding = 8) uniform ShadowUBO {
    mat4 lightSpaceTransform;
    float shadowBias;
} shadowUbo;
#endif

#ifdef USE_SAO
layout(binding = 9) uniform sampler2D aoMap;
#endif

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec4 fragBaseColor;
layout(location = 4) in vec3 fragWorldPos;
#ifdef USE_NORMAL_MAP
layout(location = 5) in vec3 fragTangent;
#endif

layout(location = 0) out vec4 outColor;

#ifdef USE_SAO
// Returns screen-space UV of the current fragment for AO map lookup.
vec2 screenUV() {
    return gl_FragCoord.xy / vec2(textureSize(aoMap, 0));
}
#endif

const float PI = 3.14159265359;

float distributionGGX(float NdotH, float roughness) {
    float a2 = pow(roughness, 4.0);
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float k = pow(roughness + 1.0, 2.0) / 8.0;
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

struct PbrShadingData {
    vec3 normal;
    vec3 viewDirection;
    vec3 albedo;
    vec3 F0;
    float NdotV;
    float metallic;
    float roughness;
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

vec3 lightContribution(GpuLight light, PbrShadingData surface) {
    LightSample ls = sampleLight(light);

    vec3 H = normalize(surface.viewDirection + ls.L);
    float NdotL = max(dot(surface.normal, ls.L), 0.0);
    float NdotH = max(dot(surface.normal, H), 0.0);
    float HdotV = max(dot(H, surface.viewDirection), 0.0);

    float D = distributionGGX(NdotH, surface.roughness);
    float G = geometrySmith(surface.NdotV, NdotL, surface.roughness);
    vec3 F = fresnelSchlick(HdotV, surface.F0);

    vec3 specular = (D * G * F) / max(4.0 * surface.NdotV * NdotL, 0.0001);
    vec3 kD = (vec3(1.0) - F) * (1.0 - surface.metallic);
    vec3 diffuse = kD * surface.albedo / PI;

    return (diffuse + specular) * light.colorAndType.xyz * ls.attenuation * NdotL;
}

#ifdef USE_IBL
// Fresnel with roughness dampening for pre-filtered environment sampling
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 shIrradiance(vec3 N) {
    return iblSH.sh[0].rgb
         + iblSH.sh[1].rgb * N.y
         + iblSH.sh[2].rgb * N.z
         + iblSH.sh[3].rgb * N.x
         + iblSH.sh[4].rgb * N.x * N.y
         + iblSH.sh[5].rgb * N.y * N.z
         + iblSH.sh[6].rgb * (3.0 * N.z * N.z - 1.0)
         + iblSH.sh[7].rgb * N.x * N.z
         + iblSH.sh[8].rgb * (N.x * N.x - N.y * N.y);
}

vec3 iblAmbient(PbrShadingData surface) {
    vec3 kS = fresnelSchlickRoughness(surface.NdotV, surface.F0, surface.roughness);
    vec3 kD = (1.0 - kS) * (1.0 - surface.metallic);

    vec3 diffuse = kD * max(shIrradiance(surface.normal), vec3(0.0)) * surface.albedo;

    vec3 R = reflect(-surface.viewDirection, surface.normal);
    // perceptualRoughnessToLod from filament/shaders/src/surface_light_indirect.fs
    float lod = 4.0 * surface.roughness * (2.0 - surface.roughness);
    vec3 prefilteredColor = textureLod(prefilterMap, R, lod).rgb;
    vec2 brdf = texture(brdfLut, vec2(surface.NdotV, 1.0 - surface.roughness)).rg;
    vec3 Fr = prefilteredColor * (kS * brdf.x + brdf.y);
    // Energy compensation (Karis 2017): corrects single-scattering energy loss at high roughness.
    float directionalAlbedo = brdf.x + brdf.y;
    vec3 energyCompensation = 1.0 + surface.F0 * (1.0 / directionalAlbedo - 1.0);
    vec3 specular = Fr * energyCompensation;

    return diffuse + specular;
}
#endif

vec3 resolveNormal() {
#ifdef USE_NORMAL_MAP
    vec3 T = normalize(fragTangent);
    vec3 N = normalize(fragNormal);
    T = normalize(T - dot(T, N) * N); // Gram-Schmidt re-orthogonalization
    vec3 B = cross(N, T);
    vec3 tsNormal = texture(normalMapSampler, fragTexCoord).rgb * 2.0 - 1.0;
    return normalize(mat3(T, B, N) * tsNormal);
#else
    return normalize(fragNormal);
#endif
}

#ifdef USE_SHADOW
float shadowVisibility() {
    vec4 fragPosLightSpace = shadowUbo.lightSpaceTransform * vec4(fragWorldPos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    float currentDepth = projCoords.z;

    if (currentDepth < 0.0 || currentDepth > 1.0)
        return 1.0;

    // Remap XY from [-1,1] to [0,1]; depth is already [0,1] in Vulkan clip space
    vec2 shadowUV = projCoords.xy * 0.5 + 0.5;

#ifdef USE_PCF
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            float pcfDepth = texture(shadowMap, shadowUV + vec2(x, y) * texelSize).r;
            shadow += (currentDepth - shadowUbo.shadowBias > pcfDepth) ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
#else
    float closestDepth = texture(shadowMap, shadowUV).r;
    return (currentDepth - shadowUbo.shadowBias > closestDepth) ? 0.0 : 1.0;
#endif
}
#endif

vec3 directLighting(PbrShadingData surface) {
    float visibility = 1.0;
#ifdef USE_SHADOW
    visibility = shadowVisibility();
#endif

    vec3 Lo = vec3(0.0);
    int numLights = int(lights.counts.x);
    for (int i = 0; i < numLights; i++) {
        float lightShadow = 1.0;
#ifdef USE_SHADOW
        lightShadow = (lights.lights[i].colorAndType.w == 1.0) ? visibility : 1.0;
#endif
        Lo += lightShadow * lightContribution(lights.lights[i], surface);
    }
    return Lo;
}

vec3 ambientLighting(PbrShadingData surface) {
    vec3 ambient = ubo.pbrParams.z * surface.albedo;
#ifdef USE_IBL
    ambient = iblAmbient(surface);
#endif

#ifdef USE_SAO
    float ao = texture(aoMap, screenUV()).r;
    ambient *= ao;
#endif
    return ambient;
}

void main() {
    vec4 texColor = texture(texSampler, fragTexCoord);
    vec3 albedo = texColor.rgb * fragBaseColor.rgb;
    vec3 normal = resolveNormal();
    if (!gl_FrontFacing) normal = -normal;
    vec3 viewDirection = normalize(ubo.cameraPos.xyz - fragWorldPos);
    PbrShadingData surface = PbrShadingData(normal, viewDirection, albedo,
            mix(vec3(0.04), albedo, ubo.pbrParams.x), max(dot(normal, viewDirection), 0.0001),
            ubo.pbrParams.x, ubo.pbrParams.y);

    vec3 direct = directLighting(surface);
    vec3 ambient = ambientLighting(surface);
    vec3 color = ambient + direct;
    outColor = vec4(color, texColor.a * fragBaseColor.a);
}
