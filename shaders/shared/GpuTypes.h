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
    uint tangent;
    vec2 uv;
};


#define TEXTURE_REF_SLOT_MASK 0xFFFFu
#define TEXTURE_REF_SAMPLER_SHIFT 16u

#define MATERIAL_BASE_COLOR_TEX   0x01u
#define MATERIAL_METAL_ROUGH_TEX  0x02u
#define MATERIAL_NORMAL_TEX       0x04u
#define MATERIAL_OCCLUSION_TEX    0x08u
#define MATERIAL_EMISSIVE_TEX     0x10u
#define MATERIAL_ALPHA_MASK       0x20u
#define MATERIAL_ALPHA_BLEND      0x40u
#define MATERIAL_DOUBLE_SIDED     0x80u

struct Material
{
    vec4 baseColorFactor;
    vec3 emissiveFactor;
    float alphaCutoff;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    uint baseColorTex;  // packed , 16 bit descriptor slot, 16 bit sampler
    uint metalRoughTex; // packed , 16 bit descriptor slot, 16 bit sampler
    uint normalTex;     // packed , 16 bit descriptor slot, 16 bit sampler
    uint occlusionTex;  // packed , 16 bit descriptor slot, 16 bit sampler
    uint emissiveTex;
    uint flags;
    uint pad0;
    uint pad1;
};

struct Instance {
    mat3x4 worldMatrix;
    uint subMesh;
    uint node;
    uint pad0;
    uint pad1;
};

#define DEBUG_VIEW_LIT         0u
#define DEBUG_VIEW_BASE_COLOR  1u
#define DEBUG_VIEW_NORMAL      2u
#define DEBUG_VIEW_ROUGHNESS   3u
#define DEBUG_VIEW_METALLIC    4u
#define DEBUG_VIEW_DIRECT      5u
#define DEBUG_VIEW_OCCLUSION   6u
#define DEBUG_VIEW_EMISSIVE    7u

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
    vec4     sunDirection;
    vec4     sunRadiance;
    uint     debugView;
    uint     pad0;
    uint     pad1;
    uint     pad2;
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

struct PostConstants
{
    uint  sourceTexture;
    uint  sourceSampler;
    float exposure;
};

#ifdef __cplusplus
static_assert(sizeof(VertexAttributes) == 16);
static_assert(offsetof(VertexAttributes, normal) == 0);
static_assert(offsetof(VertexAttributes, tangent) == 4);
static_assert(offsetof(VertexAttributes, uv) == 8);

static_assert(sizeof(Material) == 80);
static_assert(offsetof(Material, baseColorFactor) == 0);
static_assert(offsetof(Material, emissiveFactor) == 16);
static_assert(offsetof(Material, metallicFactor) == 32);
static_assert(offsetof(Material, baseColorTex) == 48);
static_assert(offsetof(Material, emissiveTex) == 64);


inline uint packTextureRef(uint textureSlot, uint samplerSlot)
{
    return (textureSlot & TEXTURE_REF_SLOT_MASK) | (samplerSlot << TEXTURE_REF_SAMPLER_SHIFT);
}
inline uint textureRefSlot(uint ref) { return ref & TEXTURE_REF_SLOT_MASK; }

static_assert(sizeof(FrameData) == 304);
static_assert(offsetof(FrameData, sunDirection) == 256);
static_assert(offsetof(FrameData, sunRadiance) == 272);
static_assert(offsetof(FrameData, debugView) == 288);
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

static_assert(sizeof(PostConstants) == 12);
static_assert(offsetof(PostConstants, sourceTexture) == 0);
static_assert(offsetof(PostConstants, sourceSampler) == 4);
static_assert(offsetof(PostConstants, exposure) == 8);

static_assert(sizeof(Instance) == 64);
static_assert(offsetof(Instance, worldMatrix) == 0);
static_assert(offsetof(Instance, subMesh) == 48);
static_assert(offsetof(Instance, node) == 52);

#endif

#endif
