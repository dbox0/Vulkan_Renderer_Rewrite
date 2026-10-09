#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "core/RangeAllocator.h"
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

struct Texture
{
    uint32_t imageId = 0;
    uint32_t samplerId = 0;
};
