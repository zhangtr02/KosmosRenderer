#pragma once

namespace kosmos::scene
{
struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

class Camera
{
public:
    const Vec3& GetPosition() const { return position_; }
    float GetYawDegrees() const { return yawDegrees_; }
    float GetPitchDegrees() const { return pitchDegrees_; }

    void SetPosition(Vec3 position) { position_ = position; }
    void AddLookDelta(float deltaX, float deltaY);
    void MoveLocal(Vec3 localDirection, float deltaSeconds);

    Vec3 Forward() const;
    Vec3 Right() const;

private:
    Vec3 position_{0.0f, 1.5f, 5.0f};
    float yawDegrees_ = -90.0f;
    float pitchDegrees_ = -10.0f;
    float movementSpeed_ = 5.0f;
    float mouseSensitivity_ = 0.12f;
};

Vec3 operator+(Vec3 left, Vec3 right);
Vec3 operator-(Vec3 left, Vec3 right);
Vec3 operator*(Vec3 vector, float scalar);
Vec3 Normalize(Vec3 vector);
Vec3 Cross(Vec3 left, Vec3 right);
}
