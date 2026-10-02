#include "GpuProfiler.h"

#include <vector>

#include "Context.h"

namespace gfx
{

void GpuProfiler::init(const Context &ctx, uint32_t framesInFlight, uint32_t maxScopes)
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(ctx.physicalDevice(), &properties);

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.physicalDevice(), &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.physicalDevice(), &familyCount, families.data());

    const uint32_t validBits = families[ctx.queueFamily()].timestampValidBits;
    if (properties.limits.timestampPeriod <= 0.0f || validBits == 0) {
        core::warn("GPU timestamps are not supported on this queue; profiler disabled");
        return;
    }

    m_device = ctx.device();
    m_maxScopes = maxScopes;
    m_period = properties.limits.timestampPeriod;
    m_validMask = validBits >= 64 ? ~uint64_t{0} : (uint64_t{1} << validBits) - 1;

    const VkQueryPoolCreateInfo poolInfo
    {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = framesInFlight * maxScopes * 2
    };
    VK_CHECK(vkCreateQueryPool(m_device, &poolInfo, nullptr, &m_pool));
    ctx.setName(VK_OBJECT_TYPE_QUERY_POOL, m_pool, "gpu timestamps");

    m_written.assign(framesInFlight, 0);
    m_names.assign(static_cast<size_t>(framesInFlight) * maxScopes, nullptr);
    m_ticks.resize(static_cast<size_t>(maxScopes) * 2);
    m_results.reserve(maxScopes);
    m_enabled = true;
}

void GpuProfiler::destroy(const Context &)
{
    if (m_pool) {
        vkDestroyQueryPool(m_device, m_pool, nullptr);
    }
    *this = GpuProfiler{};
}

void GpuProfiler::beginFrame(VkCommandBuffer cmd, uint32_t slot)
{
    m_slot = slot;
    if (!m_enabled) {
        return;
    }

    const uint32_t firstScope = slot * m_maxScopes;
    const uint32_t written = m_written[slot];
    if (written > 0) {
        const VkResult result = vkGetQueryPoolResults(m_device, m_pool, firstScope * 2, written * 2,
                                                      written * 2 * sizeof(uint64_t), m_ticks.data(),
                                                      sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
        if (result == VK_SUCCESS) {
            m_results.clear();
            for (uint32_t i = 0; i < written; ++i) {
                const uint64_t elapsed = (m_ticks[i * 2 + 1] - m_ticks[i * 2]) & m_validMask;
                const double milliseconds = static_cast<double>(elapsed) * static_cast<double>(m_period) * 1e-6;
                m_results.push_back({ m_names[firstScope + i], static_cast<float>(milliseconds) });
            }
        }
    }

    vkCmdResetQueryPool(cmd, m_pool, firstScope * 2, m_maxScopes * 2);
    m_written[slot] = 0;
}

GpuProfiler::Scope::Scope(GpuProfiler &profiler, VkCommandBuffer cmd, const char *name)
    : m_label(cmd, name), m_profiler(profiler), m_cmd(cmd), m_query(UINT32_MAX)
{
    if (!profiler.m_enabled) {
        return;
    }

    uint32_t &written = profiler.m_written[profiler.m_slot];
    if (written >= profiler.m_maxScopes) {
        if (!profiler.m_overflowed) {
            profiler.m_overflowed = true;
            core::warn("GpuProfiler: more scopes than maxScopes in one frame; the extra scopes are not timed");
        }
        return;
    }

    const uint32_t scope = profiler.m_slot * profiler.m_maxScopes + written;
    profiler.m_names[scope] = name;
    m_query = scope * 2;
    ++written;
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, profiler.m_pool, m_query);
}

GpuProfiler::Scope::~Scope()
{
    if (m_query != UINT32_MAX) {
        vkCmdWriteTimestamp2(m_cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_profiler.m_pool, m_query + 1);
    }
}

}
