#pragma once
#include <cstdint>

#include "core/Transform.h"

namespace scene {
    struct NodeHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;

        bool valid() const {return index != UINT32_MAX;}
        bool operator==(const NodeHandle &) const = default;
    };
    inline constexpr uint32_t NoParent = UINT32_MAX;

    using Transform = core::Transform;
}
