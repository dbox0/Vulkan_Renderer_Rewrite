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

struct FrameData {
    mat4        viewProj;
    mat4        view;
    mat4        proj;
    vec4        cameraPosition;
    uint64_t    positions;
    uint64_t    attributes;
    uint64_t    colors;
    uint64_t    materials;
    float       time;
    uint        frameIndex;
};

struct RenderItem
{
    mat4 worldMatrix;
    uint materialIndex;
};

struct PushConstants{
    uint64_t frame;
    uint64_t draws;
};

#ifdef __cplusplus
static_assert(sizeof(VertexAttributes) == 12);
static_assert(offsetof(VertexAttributes, normal) == 0);
static_assert(offsetof(VertexAttributes, uv) == 4);

static_assert(sizeof(Material) == 24);
static_assert(offsetof(Material, baseColor) == 0);
static_assert(offsetof(Material, textureIndex) == 16);
static_assert(offsetof(Material, samplerIndex) == 20);

static_assert(sizeof(FrameData) == 248);
static_assert(offsetof(FrameData, positions) == 208);
static_assert(offsetof(FrameData, attributes) == 216);
static_assert(offsetof(FrameData, colors) == 224);
static_assert(offsetof(FrameData, materials) == 232);
static_assert(offsetof(FrameData, time) == 240);
static_assert(offsetof(FrameData, frameIndex) == 244);

static_assert(sizeof(RenderItem) == 68);
static_assert(offsetof(RenderItem, worldMatrix) == 0);
static_assert(offsetof(RenderItem, materialIndex) == 64);

#endif

#endif
