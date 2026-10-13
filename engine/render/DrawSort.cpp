#include "DrawSort.h"
#include <algorithm>
#include <cstring>

namespace render {
    namespace {
        bool belongsTo(DrawPass pass, Bucket bucket)
        {
            return pass == DrawPass::Blended ? bucket == Bucket::Blend : bucket != Bucket::Blend;
        }

        uint32_t stateKey(DrawPass pass, const DrawRecord &record)
        {
            const uint32_t mask = pass == DrawPass::Prepass && record.bucket == Bucket::Mask ? 1u : 0u;
            return (mask << 2) | record.state;
        }
    }

    void DrawSorter::build(std::span<const DrawRecord> records, gfx::FrameArena &arena)
    {
        buildPass(DrawPass::Prepass, records, arena);
        buildPass(DrawPass::Lit, records, arena);
        buildPass(DrawPass::Blended, records, arena);
    }

    void DrawSorter::buildPass(DrawPass pass, std::span<const DrawRecord> records, gfx::FrameArena &arena)
    {
        DrawPassList &list = m_lists[static_cast<size_t>(pass)];
        list.runs.clear();
        list.count = 0;

        m_keys.clear();
        for (uint32_t i = 0; i < records.size(); ++i) {
            if (belongsTo(pass, records[i].bucket)) {
                m_keys.push_back((uint64_t{ stateKey(pass, records[i]) } << 32) | i);
            }
        }
        if (m_keys.empty()) {
            return;
        }
        std::sort(m_keys.begin(), m_keys.end());

        list.count = static_cast<uint32_t>(m_keys.size());
        list.commands = arena.allocate(m_keys.size() * sizeof(VkDrawIndexedIndirectCommand));
        auto *out = static_cast<VkDrawIndexedIndirectCommand *>(list.commands.cpu);

        uint32_t runKey = UINT32_MAX;
        for (uint32_t i = 0; i < m_keys.size(); ++i) {
            const DrawRecord &record = records[static_cast<uint32_t>(m_keys[i])];
            std::memcpy(&out[i], &record.command, sizeof(VkDrawIndexedIndirectCommand));

            const uint32_t key = stateKey(pass, record);
            const bool sameRun = key == runKey;
            runKey = key;
            if (sameRun) {
                ++list.runs.back().count;
            } else {
                list.runs.push_back(DrawRun{ .first = i, .count = 1, .bucket = record.bucket, .state = record.state });
            }
        }
    }
}
