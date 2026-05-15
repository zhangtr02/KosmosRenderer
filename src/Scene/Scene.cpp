#include "Scene/Scene.h"

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace kosmos::scene
{
namespace
{
TextureData CreateCheckerTexture()
{
    TextureData texture;
    texture.name = "DemoChecker";
    texture.width = 2;
    texture.height = 2;
    texture.srgb = true;
    texture.rgbaPixels = {
        235, 238, 245, 255,
        80, 92, 112, 255,
        80, 92, 112, 255,
        235, 238, 245, 255,
    };
    return texture;
}

TextureData CreateDefaultMetallicRoughnessTexture()
{
    TextureData texture;
    texture.name = "DefaultMetallicRoughness";
    texture.srgb = false;
    texture.rgbaPixels = {255, 255, 255, 255};
    return texture;
}

TextureData CreateDefaultNormalTexture()
{
    TextureData texture;
    texture.name = "DefaultNormal";
    texture.srgb = false;
    texture.rgbaPixels = {128, 128, 255, 255};
    return texture;
}

Mesh CreateCubeMesh(std::size_t materialIndex)
{
    const std::vector<Vertex> vertices = {
        {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
        {{1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
        {{1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
        {{-1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},

        {{1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        {{1.0f, -1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{1.0f, 1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},

        {{1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{-1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},

        {{-1.0f, -1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        {{-1.0f, -1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-1.0f, 1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{-1.0f, 1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},

        {{-1.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{1.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{-1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},

        {{-1.0f, -1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
        {{1.0f, -1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
        {{1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
        {{-1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
    };

    const std::vector<std::uint32_t> indices = {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4,
        8, 9, 10, 10, 11, 8,
        12, 13, 14, 14, 15, 12,
        16, 17, 18, 18, 19, 16,
        20, 21, 22, 22, 23, 20,
    };

    return Mesh{"DemoCubeMesh", vertices, indices, materialIndex};
}
}

Scene Scene::CreateDemoScene()
{
    Scene scene;
    const std::size_t checkerTexture = scene.AddTexture(CreateCheckerTexture());
    const std::size_t defaultMetallicRoughnessTexture = scene.AddTexture(CreateDefaultMetallicRoughnessTexture());
    const std::size_t defaultNormalTexture = scene.AddTexture(CreateDefaultNormalTexture());
    const std::size_t darkMaterial = scene.AddMaterial(Material{"BrushedDarkMetal",
                                                                Color{0.45f, 0.48f, 0.52f, 1.0f},
                                                                0.8f,
                                                                0.34f,
                                                                checkerTexture,
                                                                defaultMetallicRoughnessTexture,
                                                                defaultNormalTexture});
    const std::size_t ceramicMaterial = scene.AddMaterial(Material{"WarmCeramic",
                                                                   Color{0.85f, 0.72f, 0.58f, 1.0f},
                                                                   0.0f,
                                                                   0.62f,
                                                                   checkerTexture,
                                                                   defaultMetallicRoughnessTexture,
                                                                   defaultNormalTexture});
    const std::size_t floorMaterial = scene.AddMaterial(Material{"MatteFloor",
                                                                 Color{0.28f, 0.32f, 0.36f, 1.0f},
                                                                 0.0f,
                                                                 0.85f,
                                                                 checkerTexture,
                                                                 defaultMetallicRoughnessTexture,
                                                                 defaultNormalTexture});

    const std::size_t darkCube = scene.AddMesh(CreateCubeMesh(darkMaterial));
    const std::size_t ceramicCube = scene.AddMesh(CreateCubeMesh(ceramicMaterial));
    const std::size_t floorCube = scene.AddMesh(CreateCubeMesh(floorMaterial));

    scene.AddMeshInstance(MeshInstance{"DemoCube_Primary", darkCube, Transform{glm::vec3{-1.5f, 0.0f, 0.0f}, glm::vec3{}, glm::vec3{1.0f, 1.0f, 1.0f}}});
    scene.AddMeshInstance(MeshInstance{"DemoCube_Ceramic", ceramicCube, Transform{glm::vec3{1.5f, 0.0f, 0.0f}, glm::vec3{}, glm::vec3{1.0f, 1.0f, 1.0f}}});
    scene.AddMeshInstance(MeshInstance{"DemoFloor", floorCube, Transform{glm::vec3{0.0f, -1.1f, 0.0f}, glm::vec3{}, glm::vec3{6.0f, 0.1f, 6.0f}}});
    scene.animateDemo_ = true;
    return scene;
}

void Scene::Update(float deltaSeconds)
{
    elapsedSeconds_ += deltaSeconds;
    const float pulse = 0.5f + 0.5f * std::sin(elapsedSeconds_ * 0.35f);
    clearColor_.r = 0.012f + pulse * 0.01f;
    clearColor_.g = 0.020f + pulse * 0.01f;
    clearColor_.b = 0.040f + pulse * 0.015f;

    if (animateDemo_ && meshInstances_.size() >= 2)
    {
        meshInstances_[0].transform.rotationDegrees.y = elapsedSeconds_ * 35.0f;
        meshInstances_[1].transform.rotationDegrees.x = std::sin(elapsedSeconds_) * 12.0f;
        meshInstances_[1].transform.rotationDegrees.z = elapsedSeconds_ * -20.0f;
    }
}

std::size_t Scene::AddTexture(TextureData texture)
{
    textures_.push_back(std::move(texture));
    MarkResourcesDirty();
    return textures_.size() - 1;
}

std::size_t Scene::AddMaterial(Material material)
{
    materials_.push_back(std::move(material));
    MarkResourcesDirty();
    return materials_.size() - 1;
}

std::size_t Scene::AddMesh(Mesh mesh)
{
    meshes_.push_back(std::move(mesh));
    MarkResourcesDirty();
    return meshes_.size() - 1;
}

void Scene::AddMeshInstance(MeshInstance instance)
{
    meshInstances_.push_back(std::move(instance));
}

void Scene::MarkResourcesDirty()
{
    ++resourceVersion_;
}
}
