#ifndef BRDF_GLSL
#define BRDF_GLSL

const float PI = 3.14159265359;

float D_GGX(float NoH, float a2)
{
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float a2)
{
    float ggxV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float ggxL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / (ggxV + ggxL);
}

vec3 F_Schlick(float VoH, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);
}

vec3 evaluateDirect(vec3 n, vec3 v, vec3 l, vec3 diffuseColor, vec3 f0, float a2)
{
    float NoL = dot(n, l);
    if (NoL <= 0.0) {
        return vec3(0.0);
    }
    vec3 h = normalize(v + l);
    float NoV = max(dot(n, v), 1e-4);
    float NoH = clamp(dot(n, h), 0.0, 1.0);
    float VoH = clamp(dot(v, h), 0.0, 1.0);

    vec3 F = F_Schlick(VoH, f0);
    vec3 specular = F * D_GGX(NoH, a2) * V_SmithGGXCorrelated(NoV, NoL, a2);
    vec3 diffuse = (1.0 - F) * diffuseColor / PI;
    return (diffuse + specular) * NoL;
}

#endif
