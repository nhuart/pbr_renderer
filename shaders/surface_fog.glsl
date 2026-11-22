#ifndef SURFACE_FOG_GLSL
#define SURFACE_FOG_GLSL

#ifdef USE_FOG
#define VOLUMETRIC_DESCRIPTOR_SET 2
#include "volumetric_medium.glsl"
#endif

vec3 foggedSurfaceColor(vec3 color, vec3 worldPosition) {
#ifdef USE_FOG
    vec3 offset = worldPosition - volume.cameraPos.xyz;
    float distanceToSurface = length(offset);
    if (distanceToSurface == 0.0) {
        return color;
    }
    RaySegment ray = RaySegment(volume.cameraPos.xyz, offset / distanceToSurface, 0.0,
            min(distanceToSurface, volume.maxDistance));
    MediumIntegration medium = integrateViewMedium(ray);
    return color * medium.transmittance + mediumScatteringColor(medium);
#else
    return color;
#endif
}

#endif
