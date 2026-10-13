#version 460

#extension GL_GOOGLE_include_directive : require

#include "include/common.glsl"
#include "include/transform.glsl"

invariant gl_Position;

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec2 outUV;
layout (location = 3) out flat uint outMaterial;

void main()
{
    FrameDataPtr frame = FrameDataPtr(pc.frame);

    vec3 position = PositionPtr(frame.data.positions).positions[gl_VertexIndex];
    VertexAttributes attributes = AttributePtr(frame.data.attributes).attributes[gl_VertexIndex];
    vec4 color = unpackUnorm4x8(ColorPtr(frame.data.colors).colors[gl_VertexIndex]);
    vec3 normal = octDecode(unpackSnorm2x16(attributes.normal));

    Instance instance = InstancePtr(pc.instances).instances[gl_InstanceIndex];
    SubMeshGpu subMesh = SubMeshPtr(frame.data.subMeshes).subMeshes[instance.subMesh];

    vec3 worldPosition;
    gl_Position = transformToClip(frame.data.viewProj, instance, position, worldPosition);

    mat3 linear = transpose(mat3(instance.worldMatrix));
    mat3 cofactor = mat3(cross(linear[1], linear[2]), cross(linear[2], linear[0]), cross(linear[0], linear[1]));
    float mirror = sign(dot(linear[0], cross(linear[1], linear[2])));

    outColor = color;
    outNormal = mirror * (cofactor * normal);
    outUV = attributes.uv;
    outMaterial = subMesh.material;
}