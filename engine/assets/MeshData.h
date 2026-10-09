#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace assets {
    struct PackedAttributes
    {
        uint32_t  normal = 0;
        glm::vec2 uv{ 0.0f };
    };

    struct SubMesh
    {
        uint32_t  vertexStart = 0;
        uint32_t  vertexCount = 0;
        uint32_t  indexStart  = 0;
        uint32_t  indexCount  = 0;
        int32_t   material    = -1;
        glm::vec4 sphere{ 0.0f };
        glm::vec3 aabbMin{ 0.0f };
        glm::vec3 aabbMax{ 0.0f };
    };

    struct MeshData
    {
        std::string                   name;
        std::vector<glm::vec3>        positions;
        std::vector<PackedAttributes> attributes;
        std::vector<uint32_t>         colors;
        std::vector<uint32_t>         indices;
        std::vector<SubMesh>          subMeshes;
    };
}
