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
using mat3x4 = glm::mat3x4;
using uint = uint32_t;
#endif

struct VertexAttributes
{
    uint normal;
    vec2 uv;
};

struct Material
{
    vec4 baseColor;
    uint textureIndex;
    uint samplerIndex;
};

struct Instance {
    mat3x4 worldMatrix;
    uint subMesh;
    uint node;
    uint pad0;
    uint pad1;
};

struct FrameData
{
    mat4     viewProj;
    mat4     view;
    mat4     proj;
    vec4     cameraPosition;
    uint64_t positions;
    uint64_t attributes;
    uint64_t colors;
    uint64_t materials;
    uint64_t subMeshes;
    float    time;
    uint     frameIndex;
};

struct SubMeshGpu
{
    uint firstIndex;
    uint indexCount;
    int  vertexOffset;
    uint material;
    vec4 sphere;
    vec4 aabbMin;
    vec4 aabbMax;
};

struct PushConstants{
    uint64_t frame;
    uint64_t instances;
};

#ifdef __cplusplus
static_assert(sizeof(VertexAttributes) == 12);
static_assert(offsetof(VertexAttributes, normal) == 0);
static_assert(offsetof(VertexAttributes, uv) == 4);

static_assert(sizeof(Material) == 24);
static_assert(offsetof(Material, baseColor) == 0);
static_assert(offsetof(Material, textureIndex) == 16);
static_assert(offsetof(Material, samplerIndex) == 20);

static_assert(sizeof(FrameData) == 256);
static_assert(offsetof(FrameData, positions) == 208);
static_assert(offsetof(FrameData, attributes) == 216);
static_assert(offsetof(FrameData, colors) == 224);
static_assert(offsetof(FrameData, materials) == 232);
static_assert(offsetof(FrameData, subMeshes) == 240);
static_assert(offsetof(FrameData, time) == 248);
static_assert(offsetof(FrameData, frameIndex) == 252);

static_assert(sizeof(SubMeshGpu) == 64);
static_assert(offsetof(SubMeshGpu, firstIndex) == 0);
static_assert(offsetof(SubMeshGpu, indexCount) == 4);
static_assert(offsetof(SubMeshGpu, vertexOffset) == 8);
static_assert(offsetof(SubMeshGpu, material) == 12);
static_assert(offsetof(SubMeshGpu, sphere) == 16);
static_assert(offsetof(SubMeshGpu, aabbMin) == 32);
static_assert(offsetof(SubMeshGpu, aabbMax) == 48);

static_assert(sizeof(Instance) == 64);
static_assert(offsetof(Instance, worldMatrix) == 0);
static_assert(offsetof(Instance, subMesh) == 48);
static_assert(offsetof(Instance, node) == 52);

#endif

#endif
