#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "gfx/Barriers.h"
#include "gfx/Context.h"
#include "gfx/FrameArena.h"
#include "gfx/Resources.h"
#include "gfx/Vk.h"

namespace render
{

template <typename T>
class DeviceTable
{
public:
    void init(gfx::Context &ctx, uint32_t capacity, const char *name)
    {
        m_buffer = ctx.createBuffer(VkDeviceSize{ capacity } * sizeof(T),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            gfx::MemoryIntent::GpuOnly, name);
        m_cpu.assign(capacity, T{});
        m_isPending.assign(capacity, 0);
    }

    void destroy(gfx::Context &ctx)
    {
        ctx.destroyBuffer(m_buffer);
        m_cpu.clear();
        m_isPending.clear();
        m_pending.clear();
        m_copies.clear();
    }

    void write(uint32_t index, const T &value)
    {
        m_cpu[index] = value;
        if (!m_isPending[index]) {
            m_isPending[index] = 1;
            m_pending.push_back(index);
        }
    }

    const T &read(uint32_t index) const { return m_cpu[index]; }

    void stage(gfx::FrameArena &arena)
    {
        m_copies.clear();
        if (m_pending.empty()) {
            return;
        }
        std::sort(m_pending.begin(), m_pending.end());

        size_t runStart = 0;
        while (runStart < m_pending.size()) {
            size_t runEnd = runStart + 1;
            while (runEnd < m_pending.size() && m_pending[runEnd] == m_pending[runEnd - 1] + 1) {
                ++runEnd;
            }
            const uint32_t     first = m_pending[runStart];
            const VkDeviceSize bytes = VkDeviceSize{ runEnd - runStart } * sizeof(T);

            const gfx::ArenaAllocation allocation = arena.allocate(bytes);
            std::memcpy(allocation.cpu, &m_cpu[first], bytes);
            m_copies.push_back(VkBufferCopy{ .srcOffset = allocation.offset,
                                             .dstOffset = VkDeviceSize{ first } * sizeof(T),
                                             .size = bytes });
            m_copySource = allocation.buffer;
            runStart = runEnd;
        }

        for (const uint32_t index : m_pending) {
            m_isPending[index] = 0;
        }
        m_pending.clear();
    }

    void recordUploads(VkCommandBuffer cmd, VkPipelineStageFlags2 readers) const
    {
        if (m_copies.empty()) {
            return;
        }
        gfx::bufferBarrier(cmd, m_buffer.buffer, readers, VK_ACCESS_2_NONE,
                           VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        vkCmdCopyBuffer(cmd, m_copySource, m_buffer.buffer, static_cast<uint32_t>(m_copies.size()), m_copies.data());
        gfx::bufferBarrier(cmd, m_buffer.buffer, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           readers, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    }

    VkDeviceAddress address() const   { return m_buffer.address; }
    uint32_t        capacity() const  { return static_cast<uint32_t>(m_cpu.size()); }
    uint32_t        copyCount() const { return static_cast<uint32_t>(m_copies.size()); }

private:
    gfx::Buffer               m_buffer;
    std::vector<T>            m_cpu;
    std::vector<uint32_t>     m_pending;
    std::vector<uint8_t>      m_isPending;
    VkBuffer                  m_copySource = VK_NULL_HANDLE;
    std::vector<VkBufferCopy> m_copies;
};

}
