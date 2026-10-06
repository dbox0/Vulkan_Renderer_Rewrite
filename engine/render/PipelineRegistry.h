#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "gfx/Pipeline.h"
#include "gfx/Vk.h"

namespace gfx
{
class Context;
class ShaderCompiler;
}

namespace render
{

struct PipelineId
{
    uint32_t index = UINT32_MAX;
};

// Owns every pipeline together with what it needs to rebuild itself. Callers hold IDs, never handles.
class PipelineRegistry
{
public:
    void init(gfx::Context &ctx, const gfx::ShaderCompiler &compiler);
    void destroy();

    // aborts on failure.
    PipelineId addGraphics(std::string name, std::filesystem::path vertex, std::filesystem::path fragment,
                           gfx::GraphicsPipelineDesc desc);

    VkPipeline get(PipelineId id) const;

    void pollChanges();
    void reloadAll();

    uint32_t shaderCacheHits() const   { return m_cacheHits; }
    uint32_t shaderCacheMisses() const { return m_cacheMisses; }

private:
    struct WatchedFile
    {
        std::filesystem::path           path;
        std::filesystem::file_time_type time;
    };

    struct Entry
    {
        std::string               name;
        std::filesystem::path     vertex;
        std::filesystem::path     fragment;
        gfx::GraphicsPipelineDesc desc;       // vertex and fragment modules left null
        VkPipeline                pipeline = VK_NULL_HANDLE;
        std::vector<WatchedFile>  watched;    // both stages plus every include they opened
    };

    static constexpr std::chrono::milliseconds PollInterval{ 500 };

    bool takeChanges(Entry &entry) const;
    void reload(Entry &entry);

    // Returns VK_NULL_HANDLE on any failure, logging why.
    VkPipeline     build(const Entry &entry, std::vector<WatchedFile> &watched);
    VkShaderModule compileModule(const std::filesystem::path &path, std::vector<WatchedFile> &watched);

    gfx::Context              *m_ctx      = nullptr;
    const gfx::ShaderCompiler *m_compiler = nullptr;
    std::vector<Entry>         m_entries;
    uint32_t                   m_cacheHits   = 0;
    uint32_t                   m_cacheMisses = 0;

    std::chrono::steady_clock::time_point m_lastPoll{};
};

}
