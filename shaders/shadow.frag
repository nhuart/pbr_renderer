#version 450
#extension GL_GOOGLE_include_directive : require
#include "material_surface.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in float fragVertexAlpha;

void main() {
    applyShadowCoverage(fragTexCoord, fragVertexAlpha);
}
