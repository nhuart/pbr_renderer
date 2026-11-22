#ifndef MATERIAL_SURFACE_GLSL
#define MATERIAL_SURFACE_GLSL

layout(set = 1, binding = 0) uniform SurfaceUniforms {
    vec4 specularGlossiness;
    float alphaCutoff;
    float alphaMode;
    float workflow;
    float opacityFactor;
} surfaceMaterial;
layout(set = 1, binding = 1) uniform sampler2D surfaceColorTexture;
layout(set = 1, binding = 2) uniform sampler2D specularGlossinessTexture;

const float ALPHA_OPAQUE = 0.0;
const float ALPHA_MASK = 1.0;
const float ALPHA_BLEND = 2.0;

float materialOpacity(float textureAlpha, float vertexAlpha) {
    return textureAlpha * surfaceMaterial.opacityFactor * vertexAlpha;
}

float materialOpacity(vec2 uv, float vertexAlpha) {
    return materialOpacity(texture(surfaceColorTexture, uv).a, vertexAlpha);
}

void applyAlphaMask(float opacity) {
    if (surfaceMaterial.alphaMode == ALPHA_MASK && opacity < surfaceMaterial.alphaCutoff) discard;
}

void applyShadowCoverage(vec2 uv, float vertexAlpha) {
    // Fractional BLEND shadow coverage is approximated by a binary alpha test.
    if (surfaceMaterial.alphaMode != ALPHA_OPAQUE &&
            materialOpacity(uv, vertexAlpha) < surfaceMaterial.alphaCutoff) discard;
}

float colorPassOpacity(float textureAlpha, float vertexAlpha) {
    float opacity = materialOpacity(textureAlpha, vertexAlpha);
    applyAlphaMask(opacity);
    return surfaceMaterial.alphaMode == ALPHA_BLEND ? opacity : 1.0;
}
#endif
