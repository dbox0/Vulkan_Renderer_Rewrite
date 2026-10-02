#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "shared/GpuTypes.h"

layout(push_constant, scalar) uniform PushBlock
{
    PushConstants frameConsts;
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

void main()
{
    VertexPtr vBuffer = VertexPtr(frameConsts.vertexBufferAddress);
    Vertex v = vBuffer.vertices[gl_VertexIndex];

    RenderItemPtr riBuffer = RenderItemPtr(frameConsts.renderItemsAddress);
    RenderItem ri = riBuffer.renderItems[gl_InstanceIndex];

    MaterialPtr matBuff = MaterialPtr(frameConsts.materialBufferAddress);
    Material material = matBuff.materials[ri.materialIndex];

    gl_Position = ri.wvp * vec4(v.position, 1.0);
    outColor = v.color;
    outNormal = mat3x3(transpose(inverse(ri.worldMatrix))) * v.normal;
    outUV = v.uv;
    outTextureIndex = material.textureIndex;
    outMaterialBaseColor = material.baseColor;
}