#ifndef MEDIUM_GLSL
#define MEDIUM_GLSL

const float MEDIUM_DIRECTION_EPSILON = 1e-6;
const float MEDIUM_MAX_RAY_DISTANCE = 1e20;

struct RaySegment {
    vec3 origin;
    vec3 direction; // Normalized so distances are in world-space units.
    float startDistance;
    float endDistance;
};

bool clipRayToMediumSlab(int axis, vec3 boundsMin, vec3 boundsMax, inout RaySegment ray) {
    // A parallel ray intersects the slab only if its origin is between the boundary planes.
    if (abs(ray.direction[axis]) < MEDIUM_DIRECTION_EPSILON) {
        return !(ray.origin[axis] < boundsMin[axis] || ray.origin[axis] > boundsMax[axis]);
    }
    float distanceToMinPlane = (boundsMin[axis] - ray.origin[axis]) / ray.direction[axis];
    float distanceToMaxPlane = (boundsMax[axis] - ray.origin[axis]) / ray.direction[axis];
    ray.startDistance = max(ray.startDistance, min(distanceToMinPlane, distanceToMaxPlane));
    ray.endDistance = min(ray.endDistance, max(distanceToMinPlane, distanceToMaxPlane));
    return true;
}

bool intersectBoundedMedium(vec3 boundsMin, vec3 boundsMax, inout RaySegment ray) {
    for (int axis = 0; axis < 3; ++axis) {
        if (!clipRayToMediumSlab(axis, boundsMin, boundsMax, ray)) {
            return false;
        }
    }
    return ray.endDistance > ray.startDistance;
}

float boundedMediumTransmittance(RaySegment ray, vec3 boundsMin, vec3 boundsMax,
        float extinctionCoefficient) {
    if (extinctionCoefficient == 0.0 || !intersectBoundedMedium(boundsMin, boundsMax, ray)) {
        return 1.0;
    }
    return exp(-extinctionCoefficient * (ray.endDistance - ray.startDistance));
}

#endif
