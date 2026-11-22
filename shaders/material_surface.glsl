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

#endif
