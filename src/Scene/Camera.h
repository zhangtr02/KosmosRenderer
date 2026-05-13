#pragma once

#include <glm/vec3.hpp>

namespace kosmos::scene
{
class Camera
{
public:
    const glm::vec3& GetPosition() const { return position_; }
    float GetYawDegrees() const { return yawDegrees_; }
    float GetPitchDegrees() const { return pitchDegrees_; }

    void SetPosition(glm::vec3 position) { position_ = position; }
    void AddLookDelta(float deltaX, float deltaY);
    void MoveLocal(glm::vec3 localDirection, float deltaSeconds);

    glm::vec3 Forward() const;
    glm::vec3 Right() const;

private:
    glm::vec3 position_{0.0f, 1.5f, 5.0f};
    float yawDegrees_ = -90.0f;
    float pitchDegrees_ = -10.0f;
    float movementSpeed_ = 5.0f;
    float mouseSensitivity_ = 0.12f;
};
}
