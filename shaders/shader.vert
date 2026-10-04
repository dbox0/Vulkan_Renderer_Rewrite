#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "shared/GpuTypes.h"

layout(push_constant, scalar) uniform PushBlock
{
    PushConstants pc;
};

layout(buffer_reference, scalar) readonly buffer FrameDataPtr
{
    FrameData data;
};

layout(buffer_reference, scalar) readonly buffer VertexPtr
{
    Vertex vertices[];
};

layout(buffer_reference, scalar) readonly buffer MaterialPtr
{
    Material materials[];
};

layout(buffer_reference, scalar) readonly buffer RenderItemPtr
{
    RenderItem renderItems[];
};

layout (location = 0) out vec3 outColor;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec2 outUV;
layout (location = 3) out flat uint outTextureIndex;
layout (location = 4) out flat vec4 outMaterialBaseColor;
layout (location = 5) out flat uint outSamplerIndex;

void main()
{
    FrameDataPtr frame = FrameDataPtr(pc.frame);

    Vertex v = VertexPtr(frame.data.vertices).vertices[gl_VertexIndex];
    RenderItem ri = RenderItemPtr(pc.draws).renderItems[gl_InstanceIndex];
    Material material = MaterialPtr(frame.data.materials).materials[ri.materialIndex];

    gl_Position = frame.data.viewProj * ri.worldMatrix * vec4(v.position, 1.0);
    outColor = v.color;
    outNormal = mat3x3(transpose(inverse(ri.worldMatrix))) * v.normal;
    outUV = v.uv;
    outTextureIndex = material.textureIndex;
    outMaterialBaseColor = material.baseColor;
    outSamplerIndex = material.samplerIndex;
}