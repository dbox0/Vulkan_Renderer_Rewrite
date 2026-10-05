#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "Vk.h"

namespace gfx
{

class Context;

struct ShaderBinary
{
    std::vector<uint32_t>              spirv;
    std::vector<std::filesystem::path> includes;
    std::string                        error;
    bool                               cacheHit = false;

    bool ok() const { return error.empty(); }
};

class ShaderCompiler
{
public:
    void init(std::vector<std::filesystem::path> includeRoots, std::filesystem::path cacheDir);
    ShaderBinary compile(const std::filesystem::path &path) const;

private:
    std::vector<std::filesystem::path> m_includeRoots;
    std::filesystem::path              m_cacheDir;
    mutable bool                       m_warnedCacheWrite = false;
};

VkShaderModule createShaderModule(const Context &ctx, std::span<const uint32_t> spirv, const char *name);

}
