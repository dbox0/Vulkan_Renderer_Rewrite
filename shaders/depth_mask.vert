#version 460

#extension GL_GOOGLE_include_directive : require

#include "include/common.glsl"
#include "include/transform.glsl"

invariant gl_Position;

layout(location = 0) out vec2 outUV;
layout(location = 1) out float outVertexAlpha;
layout(location = 2) out flat uint outMaterial;

void main()
{
    FrameDataPtr frame = FrameDataPtr(pc.frame);
    vec3 position = PositionPtr(frame.data.positions).positions[gl_VertexIndex];
    Instance instance = InstancePtr(pc.instances).instances[gl_InstanceIndex];

    vec3 worldPosition;
    gl_Position = transformToClip(frame.data.viewProj, instance, position, worldPosition);

    outUV = AttributePtr(frame.data.attributes).attributes[gl_VertexIndex].uv;
    outVertexAlpha = unpackUnorm4x8(ColorPtr(frame.data.colors).colors[gl_VertexIndex]).a;
    outMaterial = SubMeshPtr(frame.data.subMeshes).subMeshes[instance.subMesh].material;
}
