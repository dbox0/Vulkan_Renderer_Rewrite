#pragma once
#include <cstdint>

struct MeshHandle {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;

    bool valid() const { return index != UINT32_MAX; }
    bool operator==(const MeshHandle &r) const = default;
};
