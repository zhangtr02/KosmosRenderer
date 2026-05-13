#pragma once

#include "Scene/Camera.h"

#include <string>
#include <vector>

namespace kosmos::scene
{
struct Color
{
    float r = 0.02f;
    float g = 0.03f;
    float b = 0.05f;
    float a = 1.0f;
};

struct Transform
{
    Vec3 translation{};
    Vec3 rotationDegrees{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct DirectionalLight
{
    Vec3 direction{-0.5f, -1.0f, -0.25f};
    Color color{1.0f, 0.96f, 0.88f, 1.0f};
    float intensity = 4.0f;
};

struct Material
{
    std::string name = "DefaultMaterial";
    Color baseColor{0.8f, 0.8f, 0.8f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
};

struct MeshInstance
{
    std::string name;
    Transform transform{};
    Material material{};
};

class Scene
{
public:
    static Scene CreateDemoScene();

    void Update(float deltaSeconds);

    Camera& GetCamera() { return camera_; }
    const Camera& GetCamera() const { return camera_; }
    const Color& GetClearColor() const { return clearColor_; }
    const DirectionalLight& GetDirectionalLight() const { return directionalLight_; }
    const std::vector<MeshInstance>& GetMeshInstances() const { return meshInstances_; }

private:
    Camera camera_{};
    Color clearColor_{0.015f, 0.025f, 0.045f, 1.0f};
    DirectionalLight directionalLight_{};
    std::vector<MeshInstance> meshInstances_;
    float elapsedSeconds_ = 0.0f;
};
}
