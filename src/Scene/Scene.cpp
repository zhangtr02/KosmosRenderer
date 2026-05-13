#include "Scene/Scene.h"

#include <cmath>

namespace kosmos::scene
{
Scene Scene::CreateDemoScene()
{
    Scene scene;
    scene.meshInstances_.push_back(MeshInstance{
        "DemoCube_Primary",
        Transform{glm::vec3{-1.5f, 0.0f, 0.0f}, glm::vec3{}, glm::vec3{1.0f, 1.0f, 1.0f}},
        Material{"BrushedDarkMetal", Color{0.45f, 0.48f, 0.52f, 1.0f}, 0.8f, 0.34f},
    });
    scene.meshInstances_.push_back(MeshInstance{
        "DemoCube_Ceramic",
        Transform{glm::vec3{1.5f, 0.0f, 0.0f}, glm::vec3{}, glm::vec3{1.0f, 1.0f, 1.0f}},
        Material{"WarmCeramic", Color{0.85f, 0.72f, 0.58f, 1.0f}, 0.0f, 0.62f},
    });
    scene.meshInstances_.push_back(MeshInstance{
        "DemoFloor",
        Transform{glm::vec3{0.0f, -1.1f, 0.0f}, glm::vec3{}, glm::vec3{6.0f, 0.1f, 6.0f}},
        Material{"MatteFloor", Color{0.28f, 0.32f, 0.36f, 1.0f}, 0.0f, 0.85f},
    });
    return scene;
}

void Scene::Update(float deltaSeconds)
{
    elapsedSeconds_ += deltaSeconds;
    const float pulse = 0.5f + 0.5f * std::sin(elapsedSeconds_ * 0.35f);
    clearColor_.r = 0.012f + pulse * 0.01f;
    clearColor_.g = 0.020f + pulse * 0.01f;
    clearColor_.b = 0.040f + pulse * 0.015f;
}
}
