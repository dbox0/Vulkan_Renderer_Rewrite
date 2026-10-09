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

layout(buffer_reference, scalar) readonly buffer PositionPtr
{
    vec3 positions[];
};

layout(buffer_reference, scalar) readonly buffer AttributePtr
{
    VertexAttributes attributes[];
};

layout(buffer_reference, scalar) readonly buffer ColorPtr
{
    uint colors[];
};

layout(buffer_reference, scalar) readonly buffer MaterialPtr
{
    Material materials[];
};

layout(buffer_reference, scalar) readonly buffer InstancePtr
{
    Instance instances[];
};

layout(buffer_reference, scalar) readonly buffer SubMeshPtr
{
    SubMeshGpu subMeshes[];
};

layout (location = 0) out vec3 outColor;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec2 outUV;
layout (location = 3) out flat uint outTextureIndex;
layout (location = 4) out flat vec4 outMaterialBaseColor;
layout (location = 5) out flat uint outSamplerIndex;

vec3 octDecode(vec2 e)
{
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = max(-n.z, 0.0);
    n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return normalize(n);
}

void main()
{
    FrameDataPtr frame = FrameDataPtr(pc.frame);

    vec3 position = PositionPtr(frame.data.positions).positions[gl_VertexIndex];
    VertexAttributes attributes = AttributePtr(frame.data.attributes).attributes[gl_VertexIndex];
    vec4 color = unpackUnorm4x8(ColorPtr(frame.data.colors).colors[gl_VertexIndex]);
    vec3 normal = octDecode(unpackSnorm2x16(attributes.normal));

    Instance instance = InstancePtr(pc.instances).instances[gl_InstanceIndex];
    SubMeshGpu subMesh = SubMeshPtr(frame.data.subMeshes).subMeshes[instance.subMesh];
    Material material = MaterialPtr(frame.data.materials).materials[subMesh.material];

    vec3 worldPosition = vec4(position, 1.0) * instance.worldMatrix;

    mat3 linear = transpose(mat3(instance.worldMatrix));
    mat3 cofactor = mat3(cross(linear[1], linear[2]), cross(linear[2], linear[0]), cross(linear[0], linear[1]));
    float mirror = sign(dot(linear[0], cross(linear[1], linear[2])));

    gl_Position = frame.data.viewProj * vec4(worldPosition, 1.0);
    outColor = color.rgb;
    outNormal = mirror * (cofactor * normal);
    outUV = attributes.uv;
    outTextureIndex = material.textureIndex;
    outMaterialBaseColor = material.baseColor;
    outSamplerIndex = material.samplerIndex;
}