#pragma once
#include <cstdint>
#include <vector>

#include "ImportedScene.h"

namespace assets {
    std::vector<MipLevel> generateMips(const uint8_t *rgba, uint32_t width, uint32_t height, ColorSpace space);
}
