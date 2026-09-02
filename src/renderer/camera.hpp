#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct Camera {
    glm::dvec3 target = { 0.0, 0.0, 0.0 };
    glm::dvec3 up = { 0.0, 0.0, 1.0 };
    double azimuth = 45.0;   // degrees, around Z
    double elevation = 35.0; // degrees, above XY plane
    double radius = 3.5;
    double fovDegrees = 45.0;
    double nearPlane = 0.1;
    double farPlane = 100.0;

    [[nodiscard]] glm::dvec3 position() const {
        double az = glm::radians(azimuth);
        double el = glm::radians(elevation);
        return target + radius * glm::dvec3(std::cos(el) * std::cos(az),
                                         std::cos(el) * std::sin(az), std::sin(el));
    }

    [[nodiscard]] glm::mat4 viewMatrix() const {
        return glm::mat4(glm::lookAt(position(), target, up));
    }

    [[nodiscard]] glm::mat4 projMatrix(float aspect) const {
        glm::mat4 proj = glm::perspective(static_cast<float>(glm::radians(fovDegrees)), aspect,
                static_cast<float>(nearPlane), static_cast<float>(farPlane));
        proj[1][1] *= -1;
        return proj;
    }

    void orbit(double dAzimuth, double dElevation) {
        azimuth += dAzimuth;
        elevation = std::clamp(elevation + dElevation, -89.0, 89.0);
    }

    void pan(double mouseDeltaX, double mouseDeltaY) {
        glm::dvec3 cameraPos = position();
        glm::dvec3 forwardDir = glm::normalize(target - cameraPos);
        glm::dvec3 rightDir = glm::normalize(glm::cross(forwardDir, up));
        glm::dvec3 viewPlaneUp = glm::normalize(glm::cross(rightDir, forwardDir));
        target += (-mouseDeltaX * rightDir + mouseDeltaY * viewPlaneUp) * radius * panScale;
    }

    void zoom(double delta) { radius = std::max(0.1, radius - delta * radius * zoomScale); }

private:
    static constexpr double panScale = 0.001;
    static constexpr double zoomScale = 0.1;
};
