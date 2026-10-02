#pragma once
#include <deque>
#include <vector>
#include <cstdint>
#include "gfx/Vk.h"
#include "gfx/Resources.h"


namespace gfx {
    class Context;
    class StagingUploader {
    public:
        struct Stats {
            uint64_t bytes = 0;
            uint32_t stalls = 0;
            uint64_t stallNs = 0;
        };

        StagingUploader() = default;
        StagingUploader(const StagingUploader &) = delete;
        StagingUploader &operator=(const StagingUploader &) = delete;

        void init(Context &ctx, VkDeviceSize ringSize);
        void destroy();

        void uploadBuffer(const Buffer &dst, VkDeviceSize dstOffset, const void* data, VkDeviceSize size);
        void uploadImage(const Image &dst, const void* pixels, VkDeviceSize size);
        uint64_t flush();

        uint64_t lastValue() const { return m_lastValue; }
        Stats takeStats();

    private:
        struct Allocation {
            void *cpu;
            VkDeviceSize offset;
        };
        struct Batch {
            uint64_t end;
            uint64_t value;
        };
        struct PooledCmd {
            VkCommandBuffer cmd;
            uint64_t value;
        };

        Allocation allocate(VkDeviceSize size, VkDeviceSize align);
        uint64_t placeAt(VkDeviceSize size, VkDeviceSize align) const;
        bool fits(uint64_t pos, VkDeviceSize size) const;
        void reclaim();
        void stallForSpace();

        VkCommandBuffer acquireCmd();
        void openBatch();
        void recordStartBarrier();
        void recordEndBarriers();

        Context *m_ctx = nullptr;
        Buffer   m_ring;
        uint8_t *m_mapped = nullptr;

        // Virtual positions
        uint64_t m_ringSize = 0;
        uint64_t m_head = 0;
        uint64_t m_tail = 0;
        std::deque<Batch> m_batches;

        VkCommandPool m_pool = VK_NULL_HANDLE;
        std::deque<PooledCmd> m_cmds;
        VkCommandBuffer m_cmd = VK_NULL_HANDLE;
        std::vector<VkImageMemoryBarrier2> m_endImageBarriers;
        std::vector<Buffer> m_oneOff;

        uint64_t m_lastValue = 0;
        Stats m_stats;
    };
}
