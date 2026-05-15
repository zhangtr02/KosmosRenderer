#pragma once

#include "Scene/Camera.h"

#include <cstddef>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

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
    glm::vec3 translation{};
    glm::vec3 rotationDegrees{};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
};

struct DirectionalLight
{
    glm::vec3 direction{-0.5f, -1.0f, -0.25f};
    Color color{1.0f, 0.96f, 0.88f, 1.0f};
    float intensity = 4.0f;
};

struct Material
{
    std::string name = "DefaultMaterial";
    Color baseColor{0.8f, 0.8f, 0.8f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    std::size_t baseColorTextureIndex = 0;
    std::size_t metallicRoughnessTextureIndex = 0;
    std::size_t normalTextureIndex = 0;
};

struct TextureData
{
    std::string name = "WhiteTexture";
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    bool srgb = true;
    std::vector<std::uint8_t> rgbaPixels{255, 255, 255, 255};
};

struct Vertex
{
    glm::vec3 position{};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 texCoord{};
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
};

struct Mesh
{
    std::string name = "Mesh";
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t materialIndex = 0;
};

struct MeshInstance
{
    std::string name;
    std::size_t meshIndex = 0;
    Transform transform{};
    glm::mat4 worldTransform{1.0f};
    bool useWorldTransform = false;
};

class Scene
{
public:
    static Scene CreateDemoScene();

    void Update(float deltaSeconds);

    std::size_t AddTexture(TextureData texture);
    std::size_t AddMaterial(Material material);
    std::size_t AddMesh(Mesh mesh);
    void AddMeshInstance(MeshInstance instance);

    Camera& GetCamera() { return camera_; }
    const Camera& GetCamera() const { return camera_; }
    const Color& GetClearColor() const { return clearColor_; }
    const DirectionalLight& GetDirectionalLight() const { return directionalLight_; }
    const std::vector<TextureData>& GetTextures() const { return textures_; }
    const std::vector<Material>& GetMaterials() const { return materials_; }
    const std::vector<Mesh>& GetMeshes() const { return meshes_; }
    const std::vector<MeshInstance>& GetMeshInstances() const { return meshInstances_; }
    std::size_t GetResourceVersion() const { return resourceVersion_; }

private:
    void MarkResourcesDirty();

    Camera camera_{};
    Color clearColor_{0.015f, 0.025f, 0.045f, 1.0f};
    DirectionalLight directionalLight_{};
    std::vector<TextureData> textures_;
    std::vector<Material> materials_;
    std::vector<Mesh> meshes_;
    std::vector<MeshInstance> meshInstances_;
    float elapsedSeconds_ = 0.0f;
    std::size_t resourceVersion_ = 0;
    bool animateDemo_ = false;
};
}
