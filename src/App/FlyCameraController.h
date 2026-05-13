#pragma once

#include "Platform/Window.h"
#include "Scene/Camera.h"

namespace kosmos::app
{
class FlyCameraController
{
public:
    void Update(platform::Window& window, scene::Camera& camera, float deltaSeconds);

private:
    bool wasCapturing_ = false;
    bool hasLastCursor_ = false;
    double lastCursorX_ = 0.0;
    double lastCursorY_ = 0.0;
};
}
