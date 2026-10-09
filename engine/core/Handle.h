#pragma once
#include <cstdint>

// A render concept that lives in core because scene and render are siblings and both need it.
struct MeshHandle {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;

    bool valid() const { return index != UINT32_MAX; }
    bool operator==(const MeshHandle &r) const = default;
};
