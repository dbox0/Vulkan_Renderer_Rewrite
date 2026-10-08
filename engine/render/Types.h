#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <glm/detail/type_quat.hpp>
#include <string>

#include "core/RangeAllocator.h"
#include "gfx/Vk.h"
#include "shared/GpuTypes.h"

struct SubMesh
{
    uint32_t  vertexStart   = 0;
    uint32_t  vertexCount   = 0;
    uint32_t  indexStart    = 0;
    uint32_t  indexCount    = 0;
    uint32_t  materialIndex = 0;
    glm::vec4 sphere{ 0.0f };
    glm::vec3 aabbMin{ 0.0f };
    glm::vec3 aabbMax{ 0.0f };
};

struct Mesh
{
    std::string          name;
    std::vector<SubMesh> subMeshes;
    core::Range          vertices;
    core::Range          indices;
    core::Range          table;
};

struct MeshData {
    std::string name;
    std::vector<glm::vec3> positions;
    std::vector<VertexAttributes> attributes;
    std::vector<uint32_t> colors;
    std::vector<uint32_t> indices;
    std::vector<SubMesh> subMeshes;
};

struct Image   // decoded pixels in RAM
{
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char *data = nullptr;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
};
struct Texture
{
    uint32_t imageId = 0;
    uint32_t samplerId = 0;
};

