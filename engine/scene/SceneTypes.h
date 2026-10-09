#pragma once
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

namespace scene {
    struct NodeHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;

        bool valid() const {return index != UINT32_MAX;}
        bool operator==(const NodeHandle &) const = default;
    };
    inline constexpr uint32_t NoParent = UINT32_MAX;

    struct Transform {
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f,0.0f,0.0f,0.0f};
        glm::vec3 scale{1.0f};
        glm::mat4 matrix() const;
    };

    inline glm::mat4 Transform::matrix() const
    {
        glm::mat4 m = glm::mat4_cast(rotation);
        m[0] *= scale.x;
        m[1] *= scale.y;
        m[2] *= scale.z;
        m[3] = glm::vec4(translation, 1.0f);
        return m;
    }
}
