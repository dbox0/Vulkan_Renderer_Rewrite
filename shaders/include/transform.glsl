#ifndef TRANSFORM_GLSL
#define TRANSFORM_GLSL

vec4 transformToClip(mat4 viewProj, Instance instance, vec3 position, out vec3 worldPosition)
{
    worldPosition = vec4(position, 1.0) * instance.worldMatrix;
    return viewProj * vec4(worldPosition, 1.0);
}

#endif