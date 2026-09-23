#pragma once

#include <filament/Camera.h>
#include <math/vec3.h>

#include <cmath>

using filament::math::float3;

struct CameraParams {
    float3 eye;
    float3 target = { 0.0f, 0.0f, 0.0f };
    float3 up = { 0.0f, 1.0f, 0.0f };
    double focalLength = 28.9706; // mm — matches glm::perspective(45° vertical, 4:3)
    double near = 0.1;
    double far = 100.0;
    float aperture = 16.0f; // f-stop; default = sunny 16 (outdoor ~100k lux)
    float shutterSpeed = 1.0f / 125.0f;
    float sensitivity = 100.0f; // ISO
};

inline void applyCamera(filament::Camera& camera, CameraParams const& params, double aspect) {
    camera.lookAt(params.eye, params.target, params.up);
    camera.setLensProjection(params.focalLength, aspect, params.near, params.far);
    camera.setExposure(params.aperture, params.shutterSpeed, params.sensitivity);
}
