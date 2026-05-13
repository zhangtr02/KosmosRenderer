#include "Scene/Camera.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace kosmos::scene
{
namespace
{
constexpr float Pi = 3.14159265358979323846f;

float ToRadians(float degrees)
{
    return degrees * Pi / 180.0f;
}
}

void Camera::AddLookDelta(float deltaX, float deltaY)
{
    yawDegrees_ += deltaX * mouseSensitivity_;
    pitchDegrees_ -= deltaY * mouseSensitivity_;
    pitchDegrees_ = std::clamp(pitchDegrees_, -89.0f, 89.0f);
}

void Camera::MoveLocal(glm::vec3 localDirection, float deltaSeconds)
{
    const glm::vec3 forward = Forward();
    const glm::vec3 right = Right();
    const glm::vec3 worldUp{0.0f, 1.0f, 0.0f};

    glm::vec3 worldDirection{};
    worldDirection += right * localDirection.x;
    worldDirection += worldUp * localDirection.y;
    worldDirection += forward * localDirection.z;
    if (glm::dot(worldDirection, worldDirection) <= 0.000001f)
    {
        return;
    }

    worldDirection = glm::normalize(worldDirection);
    position_ += worldDirection * (movementSpeed_ * deltaSeconds);
}

glm::vec3 Camera::Forward() const
{
    const float yaw = ToRadians(yawDegrees_);
    const float pitch = ToRadians(pitchDegrees_);

    glm::vec3 forward{};
    forward.x = std::cos(yaw) * std::cos(pitch);
    forward.y = std::sin(pitch);
    forward.z = std::sin(yaw) * std::cos(pitch);
    return glm::normalize(forward);
}

glm::vec3 Camera::Right() const
{
    return glm::normalize(glm::cross(Forward(), glm::vec3{0.0f, 1.0f, 0.0f}));
}
}
