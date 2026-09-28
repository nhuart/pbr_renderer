#pragma once

#include <math/mat4.h>
#include <math/vec3.h>

#include <cmath>

using filament::math::float3;
using filament::math::mat4f;

inline mat4f bunnyTransform() {
    mat4f translation = mat4f::translation(float3{ 0.03f, -1.19f, -0.28f });
    mat4f rotationY = mat4f::rotation(45.0f * float(M_PI) / 180.0f, float3{ 0, 1, 0 });
    mat4f scale = mat4f::scaling(float3{ 10.0f });
    return translation * rotationY * scale;
}

inline mat4f planeTransform() {
    mat4f translation = mat4f::translation(float3{ 0.03f, -0.857f, -0.28f });
    mat4f scale = mat4f::scaling(float3{ 3.0f });
    return translation * scale;
}

// Back wall: faces +Z toward camera
inline mat4f backWallZTransform() {
    float groundY = -0.857f;
    float halfWidth = 3.0f;
    float halfHeight = 0.375f;
    mat4f rotX = mat4f::rotation(-float(M_PI) * 0.5f, float3{ 1, 0, 0 });
    mat4f scale = mat4f::scaling(float3{ halfWidth, halfHeight, 1.0f });
    mat4f trans = mat4f::translation(float3{ 0.03f, groundY + halfHeight, -0.28f - halfWidth });
    return trans * scale * rotX;
}

// Left wall: faces +X toward camera; use rotX(-90) so winding matches backWallZ, then rotY(+90) to
// face +X
inline mat4f backWallXTransform() {
    float groundY = -0.857f;
    float halfWidth = 3.0f;
    float halfHeight = 0.375f;
    mat4f rotX = mat4f::rotation(-float(M_PI) * 0.5f, float3{ 1, 0, 0 });
    mat4f rotY = mat4f::rotation(float(M_PI) * 0.5f, float3{ 0, 1, 0 });
    mat4f scale = mat4f::scaling(float3{ halfWidth, halfHeight, 1.0f });
    mat4f trans = mat4f::translation(float3{ 0.03f - halfWidth, groundY + halfHeight, -0.28f });
    return trans * scale * rotY * rotX;
}

inline mat4f bunnyNormalMapTransform() {
    mat4f translation = mat4f::translation(float3{ -0.0925f, -0.8649f, -0.1683f });
    mat4f rotationY = mat4f::rotation(45.0f * float(M_PI) / 180.0f, float3{ 0, 1, 0 });
    mat4f scale = mat4f::scaling(float3{ 0.01562f });
    return translation * rotationY * scale;
}
