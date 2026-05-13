#include "App/FlyCameraController.h"

namespace kosmos::app
{
void FlyCameraController::Update(platform::Window& window, scene::Camera& camera, float deltaSeconds)
{
    const bool capturing = window.IsMouseButtonPressed(platform::MouseButton::Right);
    if (capturing != wasCapturing_)
    {
        window.SetCursorCaptured(capturing);
        hasLastCursor_ = false;
        wasCapturing_ = capturing;
    }

    if (!capturing)
    {
        return;
    }

    const auto cursor = window.GetCursorPosition();
    if (hasLastCursor_)
    {
        camera.AddLookDelta(static_cast<float>(cursor.x - lastCursorX_),
                            static_cast<float>(cursor.y - lastCursorY_));
    }

    lastCursorX_ = cursor.x;
    lastCursorY_ = cursor.y;
    hasLastCursor_ = true;

    scene::Vec3 localMovement{};
    if (window.IsKeyPressed(platform::Key::W))
    {
        localMovement.z += 1.0f;
    }
    if (window.IsKeyPressed(platform::Key::S))
    {
        localMovement.z -= 1.0f;
    }
    if (window.IsKeyPressed(platform::Key::D))
    {
        localMovement.x += 1.0f;
    }
    if (window.IsKeyPressed(platform::Key::A))
    {
        localMovement.x -= 1.0f;
    }
    if (window.IsKeyPressed(platform::Key::E))
    {
        localMovement.y += 1.0f;
    }
    if (window.IsKeyPressed(platform::Key::Q))
    {
        localMovement.y -= 1.0f;
    }

    camera.MoveLocal(localMovement, deltaSeconds);
}
}
