#pragma once
#include <cstdint>
#include <span>
#include <vector>

#include "DebugLabel.h"
#include "Vk.h"

namespace gfx
{

class Context;

class GpuProfiler
{
public:
    struct Result
    {
        const char *name;
        float milliseconds;
    };

    void init(const Context &ctx, uint32_t framesInFlight, uint32_t maxScopes);
    void destroy(const Context &ctx);

    void beginFrame(VkCommandBuffer cmd, uint32_t slot);
    std::span<const Result> results() const { return m_results; }

    class Scope
    {
    public:
        Scope(GpuProfiler &profiler, VkCommandBuffer cmd, const char *name);
        ~Scope();

        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;

    private:
        DebugLabel      m_label;
        GpuProfiler    &m_profiler;
        VkCommandBuffer m_cmd;
        uint32_t        m_query;
    };

private:
    VkDevice     m_device     = VK_NULL_HANDLE;
    VkQueryPool  m_pool       = VK_NULL_HANDLE;
    uint32_t     m_maxScopes  = 0;
    uint32_t     m_slot       = 0;
    float        m_period     = 0.0f;
    uint64_t     m_validMask  = 0;
    bool         m_enabled    = false;
    bool         m_overflowed = false;

    std::vector<uint32_t>     m_written;
    std::vector<const char *> m_names;
    std::vector<uint64_t>     m_ticks;
    std::vector<Result>       m_results;
};

}
