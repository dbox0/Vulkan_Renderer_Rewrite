#version 460

#extension GL_GOOGLE_include_directive : require

#include "include/common.glsl"
#include "include/brdf.glsl"

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in flat uint inMaterial;
layout(location = 4) in vec3 inWorldPosition;
layout(location = 5) in vec4 inTangent;

layout(location = 0) out vec4 fragColor;

void main()
{
    FrameDataPtr frame = FrameDataPtr(pc.frame);
    Material material = loadMaterial(inMaterial);

    vec4 baseColor = inColor * material.baseColorFactor;
    if ((material.flags & MATERIAL_BASE_COLOR_TEX) != 0u) {
        baseColor *= sampleTexture(material.baseColorTex, inUV);
    }
    float metallic = material.metallicFactor;
    float roughness = material.roughnessFactor;
    if ((material.flags & MATERIAL_METAL_ROUGH_TEX) != 0u) {
        vec4 metalRough = sampleTexture(material.metalRoughTex, inUV);
        roughness *= metalRough.g;
        metallic *= metalRough.b;
    }
    metallic = clamp(metallic, 0.0, 1.0);
    roughness = clamp(roughness, 0.045, 1.0);

    vec3 n = normalize(inNormal);
    vec3 t = normalize(inTangent.xyz - n * dot(n, inTangent.xyz));
    vec3 b = cross(n, t) * inTangent.w;
    if (!gl_FrontFacing && (material.flags & MATERIAL_DOUBLE_SIDED) != 0u) {
        n = -n;
        t = -t;
        b = -b;
    }
    if ((material.flags & MATERIAL_NORMAL_TEX) != 0u) {
        vec3 tangentNormal = sampleTexture(material.normalTex, inUV).xyz * 2.0 - 1.0;
        tangentNormal.xy *= material.normalScale;
        n = normalize(mat3(t, b, n) * tangentNormal);
    }

    float occlusion = 1.0;
    if ((material.flags & MATERIAL_OCCLUSION_TEX) != 0u) {
        occlusion = 1.0 + material.occlusionStrength * (sampleTexture(material.occlusionTex, inUV).r - 1.0);
    }
    vec3 emissive = material.emissiveFactor;
    if ((material.flags & MATERIAL_EMISSIVE_TEX) != 0u) {
        emissive *= sampleTexture(material.emissiveTex, inUV).rgb;
    }
    vec3 v = normalize(frame.data.cameraPosition.xyz - inWorldPosition);

    vec3 diffuseColor = baseColor.rgb * (1.0 - metallic);
    vec3 f0 = mix(vec3(0.04), baseColor.rgb, metallic);
    float alpha = roughness * roughness;
    float a2 = alpha * alpha;

    vec3 direct = vec3(0.0);
    if (frame.data.sunRadiance.w > 0.0) {
        direct = evaluateDirect(n, v, frame.data.sunDirection.xyz, diffuseColor, f0, a2) * frame.data.sunRadiance.rgb;
    }

    // Placeholder ambient
    vec3 hemiAmbient = mix(vec3(0.05, 0.03, 0.02), vec3(0.15, 0.18, 0.25), n.y * 0.5 + 0.5);
    vec3 color = direct + diffuseColor * hemiAmbient * occlusion + emissive;

    switch (frame.data.debugView) {
    case DEBUG_VIEW_BASE_COLOR: color = baseColor.rgb; break;
    case DEBUG_VIEW_NORMAL:     color = n * 0.5 + 0.5; break;
    case DEBUG_VIEW_ROUGHNESS:  color = vec3(roughness); break;
    case DEBUG_VIEW_METALLIC:   color = vec3(metallic); break;
    case DEBUG_VIEW_DIRECT:     color = direct; break;
    case DEBUG_VIEW_OCCLUSION:  color = vec3(occlusion); break;
    case DEBUG_VIEW_EMISSIVE:   color = emissive; break;
    default: break;
    }
    fragColor = vec4(color, baseColor.a);
}
