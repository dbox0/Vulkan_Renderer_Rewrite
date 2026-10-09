#include "InstanceSlots.h"

#include "core/Log.h"


namespace render {
    void InstanceSlots::update(std::span<const uint32_t> touched, std::span<const uint32_t> moved,
        const std::function<uint32_t(uint32_t)> &instanceCount){
        ++m_frame;
        m_dirty.clear();
        m_structureChanged = false;

        for (const uint32_t node : touched) {
            if (node >= m_entries.size()) {
                m_entries.resize(node + 1);
            }
            Entry &entry = m_entries[node];
            const uint32_t needed = instanceCount(node);
            if (entry.range.count != needed) {
                if (entry.range.count) {
                    m_allocator.free(entry.range.offset, entry.range.count);
                }
                entry.range = {};
                if (needed) {
                    if (const auto offset = m_allocator.allocate(needed)) {
                        entry.range = { *offset, needed };
                    } else {
                        core::warn("Instance buffer is full; node not drawn");
                    }
                }
                m_structureChanged = true;
            }
            if (entry.range.count) {
                markDirty(node);
            }
        }

        for (const uint32_t node : moved) {
            if (node < m_entries.size() && m_entries[node].range.count) {
                markDirty(node);
            }
        }

    }

    void InstanceSlots::reset()
    {
        m_allocator = core::RangeAllocator(m_allocator.capacity());
        m_entries.clear();
        m_dirty.clear();
        m_structureChanged = true;
    }

    core::Range InstanceSlots::range(uint32_t node) const
    {
        return node < m_entries.size() ? m_entries[node].range : core::Range{};
    }

    void InstanceSlots::markDirty(uint32_t node) {
        Entry &entry = m_entries[node];
        if (entry.markedFrame != m_frame) {
            entry.markedFrame = m_frame;
            m_dirty.push_back(node);
        }
    }
}
