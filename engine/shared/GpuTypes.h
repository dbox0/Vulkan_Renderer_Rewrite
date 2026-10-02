#ifndef GPU_TYPES_H
#define GPU_TYPES_H

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat4 = glm::mat4;
using uint = uint32_t;
#endif

struct Vertex
{
    vec3 position;
    vec3 color;
    vec3 normal;
    vec2 uv;
};

struct Material
{
    vec4 baseColor;
    uint textureIndex;
};

struct RenderItem
{
    mat4 wvp;
    mat4 worldMatrix;
    uint materialIndex;
};

struct PushConstants
{
    uint64_t vertexBufferAddress;
    uint64_t materialBufferAddress;
    uint64_t renderItemsAddress;
};

#ifdef __cplusplus
static_assert(sizeof(Vertex) == 44);
static_assert(offsetof(Vertex, position) == 0);
static_assert(offsetof(Vertex, color) == 12);
static_assert(offsetof(Vertex, normal) == 24);
static_assert(offsetof(Vertex, uv) == 36);

static_assert(sizeof(Material) == 20);
static_assert(offsetof(Material, baseColor) == 0);
static_assert(offsetof(Material, textureIndex) == 16);

static_assert(sizeof(RenderItem) == 132);
static_assert(offsetof(RenderItem, wvp) == 0);
static_assert(offsetof(RenderItem, worldMatrix) == 64);
static_assert(offsetof(RenderItem, materialIndex) == 128);

static_assert(sizeof(PushConstants) == 24);
static_assert(offsetof(PushConstants, vertexBufferAddress) == 0);
static_assert(offsetof(PushConstants, materialBufferAddress) == 8);
static_assert(offsetof(PushConstants, renderItemsAddress) == 16);
#endif

#endif
