#pragma once
#include <cstdint>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include "core/Handle.h"

namespace scene {
    struct MeshRenderer
    {
        MeshHandle mesh;
    };

    struct Light
    {
        enum class Type : uint8_t { Directional, Point, Spot };
        Type      type      = Type::Point;
        glm::vec3 color{ 1.0f };
        float     intensity = 1.0f;
        float     range     = 0.0f;   // 0 = infinite, as in KHR_lights_punctual
        float     innerCone = 0.0f;
        float     outerCone = glm::quarter_pi<float>();
    };

    // Named apart from the fly camera in Camera.h, which is input handling, not scene data.
    struct CameraComponent
    {
        float yFov  = glm::radians(60.0f);
        float zNear = 0.1f;
        float zFar  = 0.0f;           // 0 = infinite, matches the reverse-Z projection
    };
}
