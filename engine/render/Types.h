#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <glm/detail/type_quat.hpp>
#include <string>
#include "gfx/Vk.h"

struct SubMesh;

struct Vertex
{
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 color = glm::vec3(1.0f);
    glm::vec3 normal = glm::vec3(0.0f);
    glm::vec2 uv = glm::vec2(0.0f);
};

struct Mesh
{
    std::string name;
    std::vector<SubMesh> subMeshes;
};

struct SubMesh
{
    size_t vertexStart = 0;
    size_t vertexCount = 0;
    size_t indexStart = 0;
    size_t indexCount = 0;
    uint32_t materialId = 0;
};

struct Image   // decoded pixels in RAM
{
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char *data = nullptr;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
};
struct Material
{
    glm::vec4 baseColor = glm::vec4(1, 1, 1, 1);
    uint32_t textureIndex = 0;
};

struct Texture
{
    uint32_t imageId = 0;
    uint32_t samplerId = 0;
};
