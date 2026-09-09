#pragma once

#define GLFW_INCLUDE_VULKAN
#include "renderer/camera.hpp"
#include <GLFW/glfw3.h>

struct OrbitControls {
    double orbitSensitivity = 0.3;
    double panSensitivity = 0.001;
    double zoomSensitivity = 0.1;

    void mouseButton(int button, int action, double cursorX, double cursorY) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) mLeftDown = (action == GLFW_PRESS);
        if (button == GLFW_MOUSE_BUTTON_RIGHT) mRightDown = (action == GLFW_PRESS);
        if (action == GLFW_PRESS) {
            mLastX = cursorX;
            mLastY = cursorY;
        }
    }

    void mouseMove(Camera& camera, double x, double y) {
        double dx = x - mLastX;
        double dy = y - mLastY;
        mLastX = x;
        mLastY = y;

        if (mLeftDown) camera.orbit(dx * orbitSensitivity, dy * orbitSensitivity);
        else if (mRightDown)
            camera.pan(dx, dy);
    }

    void scroll(Camera& camera, double delta) { camera.zoom(delta); }

private:
    bool mLeftDown = false;
    bool mRightDown = false;
    double mLastX = 0.0;
    double mLastY = 0.0;
};
