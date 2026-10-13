#version 460

#extension GL_GOOGLE_include_directive : require

#include "include/common.glsl"
#include "include/transform.glsl"

invariant gl_Position;

void main()
{
    FrameDataPtr frame = FrameDataPtr(pc.frame);
    vec3 position = PositionPtr(frame.data.positions).positions[gl_VertexIndex];
    Instance instance = InstancePtr(pc.instances).instances[gl_InstanceIndex];

    vec3 worldPosition;
    gl_Position = transformToClip(frame.data.viewProj, instance, position, worldPosition);
}