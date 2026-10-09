#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "shared/GpuTypes.h"

layout(push_constant, scalar) uniform PushBlock
{
    PostConstants pc;
};

layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler samplers[];

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 fragColor;

void main()
{
    vec3 hdr = texture(sampler2D(textures[pc.sourceTexture], samplers[pc.sourceSampler]), inUV).rgb;
    fragColor = vec4(clamp(hdr * pc.exposure, 0.0, 1.0), 1.0);
}
