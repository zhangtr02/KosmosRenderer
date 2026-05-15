#include "Assets/GltfLoader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

namespace kosmos::assets
{
namespace
{
scene::TextureData CreateWhiteTexture()
{
    scene::TextureData texture;
    texture.name = "DefaultWhite";
    texture.width = 1;
    texture.height = 1;
    texture.rgbaPixels = {255, 255, 255, 255};
    return texture;
}

scene::Color ToColor(const std::vector<double>& values, scene::Color fallback)
{
    if (values.size() < 4)
    {
        return fallback;
    }

    return scene::Color{
        static_cast<float>(values[0]),
        static_cast<float>(values[1]),
        static_cast<float>(values[2]),
        static_cast<float>(values[3]),
    };
}

const unsigned char* AccessorData(const tinygltf::Model& model, const tinygltf::Accessor& accessor)
{
    if (accessor.bufferView < 0)
    {
        return nullptr;
    }

    const tinygltf::BufferView& bufferView = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(bufferView.buffer)];
    return buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
}

std::size_t AccessorStride(const tinygltf::Model& model, const tinygltf::Accessor& accessor)
{
    const tinygltf::BufferView& bufferView = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    const int stride = accessor.ByteStride(bufferView);
    if (stride <= 0)
    {
        throw std::runtime_error("glTF accessor has invalid byte stride.");
    }
    return static_cast<std::size_t>(stride);
}

std::vector<glm::vec3> ReadVec3Accessor(const tinygltf::Model& model, int accessorIndex)
{
    if (accessorIndex < 0)
    {
        return {};
    }

    const tinygltf::Accessor& accessor = model.accessors[static_cast<std::size_t>(accessorIndex)];
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.type != TINYGLTF_TYPE_VEC3)
    {
        throw std::runtime_error("glTF accessor must be FLOAT VEC3.");
    }

    const unsigned char* data = AccessorData(model, accessor);
    const std::size_t stride = AccessorStride(model, accessor);

    std::vector<glm::vec3> values(accessor.count);
    for (std::size_t i = 0; i < accessor.count; ++i)
    {
        const auto* element = reinterpret_cast<const float*>(data + i * stride);
        values[i] = glm::vec3{element[0], element[1], element[2]};
    }
    return values;
}

std::vector<glm::vec2> ReadVec2Accessor(const tinygltf::Model& model, int accessorIndex)
{
    if (accessorIndex < 0)
    {
        return {};
    }

    const tinygltf::Accessor& accessor = model.accessors[static_cast<std::size_t>(accessorIndex)];
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.type != TINYGLTF_TYPE_VEC2)
    {
        throw std::runtime_error("glTF accessor must be FLOAT VEC2.");
    }

    const unsigned char* data = AccessorData(model, accessor);
    const std::size_t stride = AccessorStride(model, accessor);

    std::vector<glm::vec2> values(accessor.count);
    for (std::size_t i = 0; i < accessor.count; ++i)
    {
        const auto* element = reinterpret_cast<const float*>(data + i * stride);
        values[i] = glm::vec2{element[0], element[1]};
    }
    return values;
}

std::vector<std::uint32_t> ReadIndexAccessor(const tinygltf::Model& model, int accessorIndex, std::size_t vertexCount)
{
    if (accessorIndex < 0)
    {
        std::vector<std::uint32_t> generated(vertexCount);
        for (std::size_t i = 0; i < vertexCount; ++i)
        {
            generated[i] = static_cast<std::uint32_t>(i);
        }
        return generated;
    }

    const tinygltf::Accessor& accessor = model.accessors[static_cast<std::size_t>(accessorIndex)];
    if (accessor.type != TINYGLTF_TYPE_SCALAR)
    {
        throw std::runtime_error("glTF index accessor must be SCALAR.");
    }

    const unsigned char* data = AccessorData(model, accessor);
    const std::size_t stride = AccessorStride(model, accessor);

    std::vector<std::uint32_t> indices(accessor.count);
    for (std::size_t i = 0; i < accessor.count; ++i)
    {
        const unsigned char* element = data + i * stride;
        switch (accessor.componentType)
        {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            indices[i] = *reinterpret_cast<const std::uint8_t*>(element);
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            indices[i] = *reinterpret_cast<const std::uint16_t*>(element);
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            indices[i] = *reinterpret_cast<const std::uint32_t*>(element);
            break;
        default:
            throw std::runtime_error("glTF index accessor uses an unsupported component type.");
        }
    }
    return indices;
}

void GenerateNormalsIfMissing(scene::Mesh& mesh)
{
    bool hasNormal = false;
    for (const scene::Vertex& vertex : mesh.vertices)
    {
        if (glm::length(vertex.normal) > 0.001f)
        {
            hasNormal = true;
            break;
        }
    }

    if (hasNormal)
    {
        return;
    }

    for (scene::Vertex& vertex : mesh.vertices)
    {
        vertex.normal = glm::vec3{0.0f};
    }

    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        scene::Vertex& a = mesh.vertices[mesh.indices[i + 0]];
        scene::Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        scene::Vertex& c = mesh.vertices[mesh.indices[i + 2]];
        const glm::vec3 normal = glm::normalize(glm::cross(b.position - a.position, c.position - a.position));
        a.normal += normal;
        b.normal += normal;
        c.normal += normal;
    }

    for (scene::Vertex& vertex : mesh.vertices)
    {
        vertex.normal = glm::length(vertex.normal) > 0.001f ? glm::normalize(vertex.normal) : glm::vec3{0.0f, 1.0f, 0.0f};
    }
}

scene::TextureData ConvertImage(const tinygltf::Image& image, std::string fallbackName)
{
    if (image.width <= 0 || image.height <= 0 || image.image.empty())
    {
        return CreateWhiteTexture();
    }

    scene::TextureData texture;
    texture.name = image.name.empty() ? std::move(fallbackName) : image.name;
    texture.width = static_cast<std::uint32_t>(image.width);
    texture.height = static_cast<std::uint32_t>(image.height);
    texture.rgbaPixels.resize(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4);

    const int components = std::max(1, image.component);
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height); ++pixel)
    {
        const std::size_t source = pixel * static_cast<std::size_t>(components);
        const std::size_t destination = pixel * 4;
        texture.rgbaPixels[destination + 0] = image.image[source + 0];
        texture.rgbaPixels[destination + 1] = components >= 2 ? image.image[source + 1] : image.image[source + 0];
        texture.rgbaPixels[destination + 2] = components >= 3 ? image.image[source + 2] : image.image[source + 0];
        texture.rgbaPixels[destination + 3] = components >= 4 ? image.image[source + 3] : 255;
    }

    return texture;
}

glm::mat4 NodeLocalMatrix(const tinygltf::Node& node)
{
    if (node.matrix.size() == 16)
    {
        return glm::make_mat4(node.matrix.data());
    }

    glm::mat4 transform{1.0f};
    if (node.translation.size() == 3)
    {
        transform = glm::translate(transform,
                                   glm::vec3{static_cast<float>(node.translation[0]),
                                             static_cast<float>(node.translation[1]),
                                             static_cast<float>(node.translation[2])});
    }
    if (node.rotation.size() == 4)
    {
        const glm::quat rotation{
            static_cast<float>(node.rotation[3]),
            static_cast<float>(node.rotation[0]),
            static_cast<float>(node.rotation[1]),
            static_cast<float>(node.rotation[2]),
        };
        transform *= glm::mat4_cast(rotation);
    }
    if (node.scale.size() == 3)
    {
        transform = glm::scale(transform,
                               glm::vec3{static_cast<float>(node.scale[0]),
                                         static_cast<float>(node.scale[1]),
                                         static_cast<float>(node.scale[2])});
    }
    return transform;
}

std::string MeshInstanceName(const tinygltf::Node& node, std::size_t primitiveIndex)
{
    if (!node.name.empty())
    {
        return node.name + "_Primitive" + std::to_string(primitiveIndex);
    }
    return "GltfPrimitive" + std::to_string(primitiveIndex);
}
}

scene::Scene GltfLoader::LoadStaticScene(const std::filesystem::path& path) const
{
    if (path.empty())
    {
        throw std::invalid_argument("glTF path is empty.");
    }

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string error;
    std::string warning;

    const std::string pathString = path.string();
    bool loaded = false;
    if (path.extension() == ".glb" || path.extension() == ".GLB")
    {
        loaded = loader.LoadBinaryFromFile(&model, &error, &warning, pathString);
    }
    else
    {
        loaded = loader.LoadASCIIFromFile(&model, &error, &warning, pathString);
    }

    if (!warning.empty())
    {
        std::cerr << "glTF warning: " << warning << '\n';
    }
    if (!loaded)
    {
        throw std::runtime_error("Failed to load glTF scene: " + error);
    }

    scene::Scene scene;
    const std::size_t defaultTexture = scene.AddTexture(CreateWhiteTexture());
    const std::size_t defaultMaterial = scene.AddMaterial(scene::Material{"DefaultGltfMaterial", scene::Color{0.8f, 0.8f, 0.8f, 1.0f}, 0.0f, 0.5f, defaultTexture});

    std::vector<std::size_t> textureMap(model.textures.size(), defaultTexture);
    for (std::size_t textureIndex = 0; textureIndex < model.textures.size(); ++textureIndex)
    {
        const int sourceImage = model.textures[textureIndex].source;
        if (sourceImage >= 0 && static_cast<std::size_t>(sourceImage) < model.images.size())
        {
            textureMap[textureIndex] = scene.AddTexture(ConvertImage(model.images[static_cast<std::size_t>(sourceImage)],
                                                                     "GltfTexture" + std::to_string(textureIndex)));
        }
    }

    std::vector<std::size_t> materialMap(model.materials.size(), defaultMaterial);
    for (std::size_t materialIndex = 0; materialIndex < model.materials.size(); ++materialIndex)
    {
        const tinygltf::Material& source = model.materials[materialIndex];
        scene::Material material;
        material.name = source.name.empty() ? "GltfMaterial" + std::to_string(materialIndex) : source.name;
        material.baseColor = ToColor(source.pbrMetallicRoughness.baseColorFactor, material.baseColor);
        material.metallic = static_cast<float>(source.pbrMetallicRoughness.metallicFactor);
        material.roughness = static_cast<float>(source.pbrMetallicRoughness.roughnessFactor);
        const int baseColorTexture = source.pbrMetallicRoughness.baseColorTexture.index;
        material.baseColorTextureIndex = baseColorTexture >= 0 && static_cast<std::size_t>(baseColorTexture) < textureMap.size()
                                             ? textureMap[static_cast<std::size_t>(baseColorTexture)]
                                             : defaultTexture;
        materialMap[materialIndex] = scene.AddMaterial(std::move(material));
    }

    std::vector<std::vector<std::size_t>> primitiveMeshesByGltfMesh(model.meshes.size());
    for (std::size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex)
    {
        const tinygltf::Mesh& sourceMesh = model.meshes[meshIndex];
        for (std::size_t primitiveIndex = 0; primitiveIndex < sourceMesh.primitives.size(); ++primitiveIndex)
        {
            const tinygltf::Primitive& primitive = sourceMesh.primitives[primitiveIndex];
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
            {
                continue;
            }

            const auto positionAttribute = primitive.attributes.find("POSITION");
            if (positionAttribute == primitive.attributes.end())
            {
                continue;
            }

            const std::vector<glm::vec3> positions = ReadVec3Accessor(model, positionAttribute->second);

            std::vector<glm::vec3> normals;
            const auto normalAttribute = primitive.attributes.find("NORMAL");
            if (normalAttribute != primitive.attributes.end())
            {
                normals = ReadVec3Accessor(model, normalAttribute->second);
            }

            std::vector<glm::vec2> texCoords;
            const auto texCoordAttribute = primitive.attributes.find("TEXCOORD_0");
            if (texCoordAttribute != primitive.attributes.end())
            {
                texCoords = ReadVec2Accessor(model, texCoordAttribute->second);
            }

            scene::Mesh mesh;
            mesh.name = sourceMesh.name.empty()
                            ? "GltfMesh" + std::to_string(meshIndex) + "_Primitive" + std::to_string(primitiveIndex)
                            : sourceMesh.name + "_Primitive" + std::to_string(primitiveIndex);
            mesh.materialIndex = primitive.material >= 0 && static_cast<std::size_t>(primitive.material) < materialMap.size()
                                     ? materialMap[static_cast<std::size_t>(primitive.material)]
                                     : defaultMaterial;
            mesh.vertices.resize(positions.size());
            for (std::size_t vertexIndex = 0; vertexIndex < positions.size(); ++vertexIndex)
            {
                mesh.vertices[vertexIndex].position = positions[vertexIndex];
                if (vertexIndex < normals.size())
                {
                    mesh.vertices[vertexIndex].normal = normals[vertexIndex];
                }
                if (vertexIndex < texCoords.size())
                {
                    mesh.vertices[vertexIndex].texCoord = texCoords[vertexIndex];
                }
            }
            mesh.indices = ReadIndexAccessor(model, primitive.indices, mesh.vertices.size());
            GenerateNormalsIfMissing(mesh);

            const std::size_t sceneMeshIndex = scene.AddMesh(std::move(mesh));
            primitiveMeshesByGltfMesh[meshIndex].push_back(sceneMeshIndex);
        }
    }

    bool instantiatedNodeMesh = false;
    const auto traverseNode = [&](auto&& self, int nodeIndex, const glm::mat4& parentTransform) -> void {
        if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= model.nodes.size())
        {
            return;
        }

        const tinygltf::Node& node = model.nodes[static_cast<std::size_t>(nodeIndex)];
        const glm::mat4 worldTransform = parentTransform * NodeLocalMatrix(node);

        if (node.mesh >= 0 && static_cast<std::size_t>(node.mesh) < primitiveMeshesByGltfMesh.size())
        {
            const std::vector<std::size_t>& primitiveMeshes = primitiveMeshesByGltfMesh[static_cast<std::size_t>(node.mesh)];
            for (std::size_t primitiveIndex = 0; primitiveIndex < primitiveMeshes.size(); ++primitiveIndex)
            {
                scene.AddMeshInstance(scene::MeshInstance{
                    MeshInstanceName(node, primitiveIndex),
                    primitiveMeshes[primitiveIndex],
                    {},
                    worldTransform,
                    true,
                });
                instantiatedNodeMesh = true;
            }
        }

        for (int child : node.children)
        {
            self(self, child, worldTransform);
        }
    };

    const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (!model.scenes.empty() && sceneIndex >= 0 && static_cast<std::size_t>(sceneIndex) < model.scenes.size())
    {
        for (int nodeIndex : model.scenes[static_cast<std::size_t>(sceneIndex)].nodes)
        {
            traverseNode(traverseNode, nodeIndex, glm::mat4{1.0f});
        }
    }

    if (!instantiatedNodeMesh)
    {
        const std::vector<scene::Mesh>& meshes = scene.GetMeshes();
        for (std::size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
        {
            scene.AddMeshInstance(scene::MeshInstance{"GltfMeshInstance" + std::to_string(meshIndex), meshIndex});
        }
    }

    std::cout << "Loaded glTF scene: " << pathString << " ("
              << scene.GetMeshes().size() << " meshes, "
              << scene.GetMaterials().size() << " materials, "
              << scene.GetTextures().size() << " textures)\n";
    return scene;
}
}
