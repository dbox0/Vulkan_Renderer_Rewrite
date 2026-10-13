#version 460

#extension GL_GOOGLE_include_directive : require

#include "include/common.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 1) in float inVertexAlpha;
layout(location = 2) in flat uint inMaterial;

void main()
{
    Material material = loadMaterial(inMaterial);
    float alpha = material.baseColorFactor.a * inVertexAlpha;
    if ((material.flags & MATERIAL_BASE_COLOR_TEX) != 0u) {
        alpha *= sampleTexture(material.baseColorTex, inUV).a;
    }
    if (alpha < material.alphaCutoff) {
        discard;
    }
}
