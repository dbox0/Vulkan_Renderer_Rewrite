#pragma once
#include <cstdint>
#include <string_view>

namespace core
{

constexpr uint64_t FnvOffset = 14695981039346656037ull;
constexpr uint64_t FnvPrime  = 1099511628211ull;

// FNV-1a, 64-bit. Previous result as seed.
constexpr uint64_t fnv1a64(std::string_view text, uint64_t seed = FnvOffset)
{
    uint64_t hash = seed;
    for (const char c : text) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= FnvPrime;
    }
    return hash;
}

static_assert(fnv1a64("") == 0xcbf29ce484222325ull);
static_assert(fnv1a64("a") == 0xaf63dc4c8601ec8cull);

}
