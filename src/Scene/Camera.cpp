#include "Scene/Camera.h"

#include <algorithm>
#include <cmath>

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

void Camera::MoveLocal(Vec3 localDirection, float deltaSeconds)
{
    const Vec3 forward = Forward();
    const Vec3 right = Right();
    const Vec3 worldUp{0.0f, 1.0f, 0.0f};

    Vec3 worldDirection{};
    worldDirection = worldDirection + right * localDirection.x;
    worldDirection = worldDirection + worldUp * localDirection.y;
    worldDirection = worldDirection + forward * localDirection.z;
    worldDirection = Normalize(worldDirection);

    position_ = position_ + worldDirection * (movementSpeed_ * deltaSeconds);
}

Vec3 Camera::Forward() const
{
    const float yaw = ToRadians(yawDegrees_);
    const float pitch = ToRadians(pitchDegrees_);

    Vec3 forward{};
    forward.x = std::cos(yaw) * std::cos(pitch);
    forward.y = std::sin(pitch);
    forward.z = std::sin(yaw) * std::cos(pitch);
    return Normalize(forward);
}

Vec3 Camera::Right() const
{
    return Normalize(Cross(Forward(), Vec3{0.0f, 1.0f, 0.0f}));
}

Vec3 operator+(Vec3 left, Vec3 right)
{
    return Vec3{left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 operator-(Vec3 left, Vec3 right)
{
    return Vec3{left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 operator*(Vec3 vector, float scalar)
{
    return Vec3{vector.x * scalar, vector.y * scalar, vector.z * scalar};
}

Vec3 Normalize(Vec3 vector)
{
    const float lengthSquared = vector.x * vector.x + vector.y * vector.y + vector.z * vector.z;
    if (lengthSquared <= 0.000001f)
    {
        return Vec3{};
    }

    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    return vector * inverseLength;
}

Vec3 Cross(Vec3 left, Vec3 right)
{
    return Vec3{
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}
}
