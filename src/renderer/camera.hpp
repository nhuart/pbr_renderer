#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct Camera {
    glm::vec3 position   = {2.0f, 2.0f, 2.0f};
    glm::vec3 target     = {0.0f, 0.0f, 0.0f};
    glm::vec3 up         = {0.0f, 0.0f, 1.0f};
    float     fovDegrees = 45.0f;
    float     nearPlane  = 0.1f;
    float     farPlane   = 10.0f;

    [[nodiscard]] glm::mat4 viewMatrix() const {
        return glm::lookAt(position, target, up);
    }

    [[nodiscard]] glm::mat4 projMatrix(float aspect) const {
        glm::mat4 proj = glm::perspective(glm::radians(fovDegrees), aspect, nearPlane, farPlane);
        proj[1][1] *= -1;
        return proj;
    }
};
