#version 460

#extension GL_GOOGLE_include_directive : require

#include "include/common.glsl"

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in flat uint inMaterial;

layout(location = 0) out vec4 fragColor;

void main()
{
    Material material = loadMaterial(inMaterial);

    vec3 nNormal = normalize(inNormal);
    if (!gl_FrontFacing && (material.flags & MATERIAL_DOUBLE_SIDED) != 0u) {
        nNormal = -nNormal;
    }
    vec3 lightDirection = normalize(vec3(0, -1, -1));
    float d = max(dot(nNormal, -lightDirection), 0);
    vec4 texColor = sampleTexture(material.baseColorTex, inUV);

    // two-tone ambient light
    vec3 skyColor = vec3(0.15, 0.18, 0.25);
    vec3 groundColor = vec3(0.05, 0.03, 0.02);
    float t = nNormal.y * 0.5 + 0.5;
    vec3 hemiAmbient = mix(groundColor, skyColor, t);

    vec4 baseColor = inColor * texColor * material.baseColorFactor;
    vec3 litColor = baseColor.rgb * d + baseColor.rgb * hemiAmbient;
    fragColor = vec4(litColor, baseColor.a);
}
