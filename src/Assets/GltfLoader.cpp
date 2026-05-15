#include "Assets/GltfLoader.h"

#include <algorithm>
#include <cmath>
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
#include <glm/vec4.hpp>

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
    texture.srgb = true;
    texture.rgbaPixels = {255, 255, 255, 255};
    return texture;
}

scene::TextureData CreateDefaultMetallicRoughnessTexture()
{
    scene::TextureData texture;
    texture.name = "DefaultMetallicRoughness";
    texture.srgb = false;
    texture.rgbaPixels = {255, 255, 255, 255};
    return texture;
}

scene::TextureData CreateDefaultNormalTexture()
{
    scene::TextureData texture;
    texture.name = "DefaultNormal";
    texture.srgb = false;
    texture.rgbaPixels = {128, 128, 255, 255};
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

std::vector<glm::vec4> ReadVec4Accessor(const tinygltf::Model& model, int accessorIndex)
{
    if (accessorIndex < 0)
    {
        return {};
    }

    const tinygltf::Accessor& accessor = model.accessors[static_cast<std::size_t>(accessorIndex)];
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.type != TINYGLTF_TYPE_VEC4)
    {
        throw std::runtime_error("glTF accessor must be FLOAT VEC4.");
    }

    const unsigned char* data = AccessorData(model, accessor);
    const std::size_t stride = AccessorStride(model, accessor);

    std::vector<glm::vec4> values(accessor.count);
    for (std::size_t i = 0; i < accessor.count; ++i)
    {
        const auto* element = reinterpret_cast<const float*>(data + i * stride);
        values[i] = glm::vec4{element[0], element[1], element[2], element[3]};
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

void GenerateTangentsIfMissing(scene::Mesh& mesh)
{
    bool hasTangent = false;
    for (const scene::Vertex& vertex : mesh.vertices)
    {
        if (glm::length(glm::vec3{vertex.tangent}) > 0.001f)
        {
            hasTangent = true;
            break;
        }
    }

    if (hasTangent)
    {
        return;
    }

    std::vector<glm::vec3> tangents(mesh.vertices.size(), glm::vec3{0.0f});
    std::vector<glm::vec3> bitangents(mesh.vertices.size(), glm::vec3{0.0f});

    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const std::uint32_t i0 = mesh.indices[i + 0];
        const std::uint32_t i1 = mesh.indices[i + 1];
        const std::uint32_t i2 = mesh.indices[i + 2];

        const scene::Vertex& v0 = mesh.vertices[i0];
        const scene::Vertex& v1 = mesh.vertices[i1];
        const scene::Vertex& v2 = mesh.vertices[i2];

        const glm::vec3 edge1 = v1.position - v0.position;
        const glm::vec3 edge2 = v2.position - v0.position;
        const glm::vec2 deltaUv1 = v1.texCoord - v0.texCoord;
        const glm::vec2 deltaUv2 = v2.texCoord - v0.texCoord;
        const float determinant = deltaUv1.x * deltaUv2.y - deltaUv2.x * deltaUv1.y;
        if (std::abs(determinant) < 0.000001f)
        {
            continue;
        }

        const float inverseDeterminant = 1.0f / determinant;
        const glm::vec3 tangent = (edge1 * deltaUv2.y - edge2 * deltaUv1.y) * inverseDeterminant;
        const glm::vec3 bitangent = (edge2 * deltaUv1.x - edge1 * deltaUv2.x) * inverseDeterminant;

        tangents[i0] += tangent;
        tangents[i1] += tangent;
        tangents[i2] += tangent;
        bitangents[i0] += bitangent;
        bitangents[i1] += bitangent;
        bitangents[i2] += bitangent;
    }

    for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        const glm::vec3 normal = glm::normalize(mesh.vertices[i].normal);
        glm::vec3 tangent = tangents[i];
        if (glm::length(tangent) < 0.001f)
        {
            tangent = std::abs(normal.y) < 0.999f
                          ? glm::normalize(glm::cross(glm::vec3{0.0f, 1.0f, 0.0f}, normal))
                          : glm::normalize(glm::cross(glm::vec3{1.0f, 0.0f, 0.0f}, normal));
        }

        tangent = glm::normalize(tangent - normal * glm::dot(normal, tangent));
        const float handedness = glm::dot(glm::cross(normal, tangent), bitangents[i]) < 0.0f ? -1.0f : 1.0f;
        mesh.vertices[i].tangent = glm::vec4{tangent, handedness};
    }
}

scene::TextureData ConvertImage(const tinygltf::Image& image, std::string fallbackName, bool srgb)
{
    if (image.width <= 0 || image.height <= 0 || image.image.empty())
    {
        return CreateWhiteTexture();
    }

    scene::TextureData texture;
    texture.name = image.name.empty() ? std::move(fallbackName) : image.name;
    texture.width = static_cast<std::uint32_t>(image.width);
    texture.height = static_cast<std::uint32_t>(image.height);
    texture.srgb = srgb;
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
    const std::size_t defaultMetallicRoughnessTexture = scene.AddTexture(CreateDefaultMetallicRoughnessTexture());
    const std::size_t defaultNormalTexture = scene.AddTexture(CreateDefaultNormalTexture());
    const std::size_t defaultMaterial = scene.AddMaterial(scene::Material{"DefaultGltfMaterial",
                                                                          scene::Color{0.8f, 0.8f, 0.8f, 1.0f},
                                                                          0.0f,
                                                                          0.5f,
                                                                          defaultTexture,
                                                                          defaultMetallicRoughnessTexture,
                                                                          defaultNormalTexture});

    std::vector<std::size_t> srgbTextureMap(model.textures.size(), defaultTexture);
    std::vector<std::size_t> linearTextureMap(model.textures.size(), defaultMetallicRoughnessTexture);
    for (std::size_t textureIndex = 0; textureIndex < model.textures.size(); ++textureIndex)
    {
        const int sourceImage = model.textures[textureIndex].source;
        if (sourceImage >= 0 && static_cast<std::size_t>(sourceImage) < model.images.size())
        {
            srgbTextureMap[textureIndex] = scene.AddTexture(ConvertImage(model.images[static_cast<std::size_t>(sourceImage)],
                                                                         "GltfSrgbTexture" + std::to_string(textureIndex),
                                                                         true));
            linearTextureMap[textureIndex] = scene.AddTexture(ConvertImage(model.images[static_cast<std::size_t>(sourceImage)],
                                                                           "GltfLinearTexture" + std::to_string(textureIndex),
                                                                           false));
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
        material.baseColorTextureIndex = baseColorTexture >= 0 && static_cast<std::size_t>(baseColorTexture) < srgbTextureMap.size()
                                             ? srgbTextureMap[static_cast<std::size_t>(baseColorTexture)]
                                             : defaultTexture;
        const int metallicRoughnessTexture = source.pbrMetallicRoughness.metallicRoughnessTexture.index;
        material.metallicRoughnessTextureIndex = metallicRoughnessTexture >= 0 && static_cast<std::size_t>(metallicRoughnessTexture) < linearTextureMap.size()
                                                     ? linearTextureMap[static_cast<std::size_t>(metallicRoughnessTexture)]
                                                     : defaultMetallicRoughnessTexture;
        const int normalTexture = source.normalTexture.index;
        material.normalTextureIndex = normalTexture >= 0 && static_cast<std::size_t>(normalTexture) < linearTextureMap.size()
                                          ? linearTextureMap[static_cast<std::size_t>(normalTexture)]
                                          : defaultNormalTexture;
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

            std::vector<glm::vec4> tangents;
            const auto tangentAttribute = primitive.attributes.find("TANGENT");
            if (tangentAttribute != primitive.attributes.end())
            {
                tangents = ReadVec4Accessor(model, tangentAttribute->second);
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
                if (vertexIndex < tangents.size())
                {
                    mesh.vertices[vertexIndex].tangent = tangents[vertexIndex];
                }
                else
                {
                    mesh.vertices[vertexIndex].tangent = glm::vec4{0.0f};
                }
            }
            mesh.indices = ReadIndexAccessor(model, primitive.indices, mesh.vertices.size());
            GenerateNormalsIfMissing(mesh);
            GenerateTangentsIfMissing(mesh);

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
