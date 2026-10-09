#pragma once
#include <cstdint>
#include <functional>

namespace core {
    void parallelFor(uint32_t count, const std::function<void(uint32_t)> &body);
}
