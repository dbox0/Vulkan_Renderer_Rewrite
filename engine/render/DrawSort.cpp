#include "DrawSort.h"
#include <algorithm>
#include <bit>
#include <cstring>

#include <glm/geometric.hpp>

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

        uint32_t depthBits(float depth)
        {
            return std::bit_cast<uint32_t>(std::max(depth, 0.0f));
        }

        constexpr uint32_t MaterialBits = 12;

        //  Prepass: [63..61] state | [60..32] depth, near first                  | [31..0] record
        //  Lit:     [63..61] state | [60..49] material | [48..32] coarse depth   | [31..0] record
        //  Blended: [63..32] depth, far first                                    | [31..0] record
        uint64_t sortKey(DrawPass pass, const DrawRecord &record, uint32_t depth, uint32_t index)
        {
            const uint64_t state = stateKey(pass, record);
            uint64_t high = 0;
            switch (pass) {
            case DrawPass::Prepass:
                high = (state << 29) | (depth >> 2);
                break;
            case DrawPass::Lit:
                high = (state << 29) | (uint64_t{ record.material & ((1u << MaterialBits) - 1) } << 17) | (depth >> 14);
                break;
            case DrawPass::Blended:
                high = ~depth;
                break;
            }
            return (high << 32) | index;
        }
    }

    void DrawSorter::build(std::span<const DrawRecord> records, std::span<const glm::vec3> centers,
                           const SortView &view, gfx::FrameArena &arena)
    {
        m_depthBits.resize(records.size());
        for (size_t i = 0; i < records.size(); ++i) {
            const glm::vec3 &center = centers[records[i].command.firstInstance];
            m_depthBits[i] = depthBits(glm::dot(center - view.position, view.forward));
        }
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
                m_keys.push_back(sortKey(pass, records[i], m_depthBits[i], i));
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
