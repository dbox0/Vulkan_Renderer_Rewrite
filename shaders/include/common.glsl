#ifndef COMMON_GLSL
#define COMMON_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

#include "shared/GpuTypes.h"

layout(push_constant, scalar) uniform PushBlock
{
    PushConstants pc;
};

layout(buffer_reference, scalar) readonly buffer FrameDataPtr  { FrameData data; };
layout(buffer_reference, scalar) readonly buffer PositionPtr   { vec3 positions[]; };
layout(buffer_reference, scalar) readonly buffer AttributePtr  { VertexAttributes attributes[]; };
layout(buffer_reference, scalar) readonly buffer ColorPtr      { uint colors[]; };
layout(buffer_reference, scalar) readonly buffer MaterialPtr   { Material materials[]; };
layout(buffer_reference, scalar) readonly buffer InstancePtr   { Instance instances[]; };
layout(buffer_reference, scalar) readonly buffer SubMeshPtr    { SubMeshGpu subMeshes[]; };

layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler samplers[];

Material loadMaterial(uint index)
{
    return MaterialPtr(FrameDataPtr(pc.frame).data.materials).materials[index];
}

vec4 sampleTexture(uint ref, vec2 uv)
{
    uint slot = ref & TEXTURE_REF_SLOT_MASK;
    uint samplerSlot = ref >> TEXTURE_REF_SAMPLER_SHIFT;
    return texture(sampler2D(textures[nonuniformEXT(slot)], samplers[nonuniformEXT(samplerSlot)]), uv);
}

vec3 octDecode(vec2 e)
{
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = max(-n.z, 0.0);
    n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return normalize(n);
}

#endif