#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct Camera {
    glm::dvec3 target;
    glm::dvec3 up = { 0.0, 1.0, 0.0 };
    double azimuth;   // degrees, around Y
    double elevation; // degrees, above XZ plane
    double radius;
    double fovDegrees;
    double nearPlane;
    double farPlane;
    double aperture     = 16.0;  // f-stop
    double shutterSpeed = 1.0 / 125.0;
    double sensitivity  = 100.0; // ISO

    [[nodiscard]] glm::dvec3 position() const {
        double az = glm::radians(azimuth);
        double el = glm::radians(elevation);
        return target + radius * glm::dvec3(std::cos(el) * std::cos(az), std::sin(el),
                                         std::cos(el) * std::sin(az));
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
