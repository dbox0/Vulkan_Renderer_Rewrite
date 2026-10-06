#include "PipelineRegistry.h"

#include <algorithm>
#include <cassert>
#include <format>

#include "gfx/Context.h"
#include "gfx/ShaderCompiler.h"

namespace render
{

void PipelineRegistry::init(gfx::Context &ctx, const gfx::ShaderCompiler &compiler)
{
    m_ctx = &ctx;
    m_compiler = &compiler;
}

void PipelineRegistry::destroy()
{
    for (Entry &entry : m_entries) {
        vkDestroyPipeline(m_ctx->device(), entry.pipeline, nullptr);
    }
    m_entries.clear();
}

PipelineId PipelineRegistry::addGraphics(std::string name, std::filesystem::path vertex,
                                         std::filesystem::path fragment, gfx::GraphicsPipelineDesc desc)
{
    desc.vertex = VK_NULL_HANDLE;
    desc.fragment = VK_NULL_HANDLE;
    Entry entry
    {
        .name = std::move(name),
        .vertex = std::move(vertex),
        .fragment = std::move(fragment),
        .desc = std::move(desc)
    };

    std::vector<WatchedFile> watched;
    entry.pipeline = build(entry, watched);
    if (!entry.pipeline) {
        core::fatal(std::format("Cannot create pipeline {}", entry.name));
    }
    entry.watched = std::move(watched);

    m_entries.push_back(std::move(entry));
    return PipelineId{ static_cast<uint32_t>(m_entries.size() - 1) };
}

VkPipeline PipelineRegistry::get(PipelineId id) const
{
    assert(id.index < m_entries.size());
    return m_entries[id.index].pipeline;
}

void PipelineRegistry::pollChanges()
{
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastPoll < PollInterval) {
        return;
    }
    m_lastPoll = now;

    for (Entry &entry : m_entries) {
        if (takeChanges(entry)) {
            reload(entry);
        }
    }
}

void PipelineRegistry::reloadAll()
{
    for (Entry &entry : m_entries) {
        takeChanges(entry);
        reload(entry);
    }
}

bool PipelineRegistry::takeChanges(Entry &entry) const
{
    std::vector<std::filesystem::file_time_type> times;
    times.reserve(entry.watched.size());
    for (const WatchedFile &file : entry.watched) {
        std::error_code ec;
        times.push_back(std::filesystem::last_write_time(file.path, ec));
        if (ec) {
            return false;   // probably an editor's save-by-rename in progress; try again next poll
        }
    }

    bool changed = false;
    for (size_t i = 0; i < times.size(); ++i) {
        changed |= entry.watched[i].time != times[i];
        entry.watched[i].time = times[i];
    }
    return changed;
}

void PipelineRegistry::reload(Entry &entry)
{
    const auto start = std::chrono::steady_clock::now();

    std::vector<WatchedFile> watched;
    const VkPipeline pipeline = build(entry, watched);
    if (!pipeline) {
        core::warn(std::format("Reloading {} failed; the previous pipeline stays", entry.name));
        return;
    }

    // Frames in flight may still have the old pipeline bound.
    m_ctx->retire([device = m_ctx->device(), old = entry.pipeline] { vkDestroyPipeline(device, old, nullptr); });
    entry.pipeline = pipeline;
    entry.watched = std::move(watched);

    const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
    core::log(std::format("Reloaded {} in {:.1f} ms", entry.name, elapsed.count()));
}

VkPipeline PipelineRegistry::build(const Entry &entry, std::vector<WatchedFile> &watched)
{
    watched.clear();
    const VkShaderModule vertex = compileModule(entry.vertex, watched);
    const VkShaderModule fragment = vertex ? compileModule(entry.fragment, watched) : VK_NULL_HANDLE;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vertex && fragment) {
        gfx::GraphicsPipelineDesc desc = entry.desc;
        desc.vertex = vertex;
        desc.fragment = fragment;
        pipeline = gfx::tryCreateGraphicsPipeline(*m_ctx, desc, entry.name.c_str());
    }

    // pipeline doesn't need its modules once it exists.
    vkDestroyShaderModule(m_ctx->device(), vertex, nullptr);
    vkDestroyShaderModule(m_ctx->device(), fragment, nullptr);
    return pipeline;
}

VkShaderModule PipelineRegistry::compileModule(const std::filesystem::path &path, std::vector<WatchedFile> &watched)
{
    // Read time before compiling: save landing mid-compile shows up on the next poll.
    std::error_code ec;
    watched.push_back({ path, std::filesystem::last_write_time(path, ec) });

    const gfx::ShaderBinary binary = m_compiler->compile(path);
    if (!binary.ok()) {
        core::warn(std::format("Shader {} failed:\n{}", path.filename().string(), binary.error));
        return VK_NULL_HANDLE;
    }
    ++(binary.cacheHit ? m_cacheHits : m_cacheMisses);

    std::string includes;
    for (const std::filesystem::path &include : binary.includes) {
        includes += std::format(" {}", include.string());
        const bool known = std::ranges::any_of(watched, [&](const WatchedFile &file) { return file.path == include; });
        if (!known) {
            watched.push_back({ include, std::filesystem::last_write_time(include, ec) });
        }
    }
    const std::string name = path.filename().string();
    core::log(std::format("Shader {} ({}), includes:{}", name, binary.cacheHit ? "cache hit" : "compiled",
                          includes.empty() ? " none" : includes));

    return gfx::createShaderModule(*m_ctx, binary.spirv, name.c_str());
}

}
