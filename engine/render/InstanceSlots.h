#pragma once
#include <functional>
#include <span>
#include <glm/fwd.hpp>
#include <cstdint>
#include "core/RangeAllocator.h"

namespace render {
    class InstanceSlots {
    public:
        explicit InstanceSlots(uint32_t capacity): m_allocator(capacity) {}

        void update(std::span<const uint32_t> touched, std::span<const uint32_t> moved,
                const std::function<uint32_t(uint32_t)> &instanceCount);
        void reset();

        std::span<const uint32_t> dirty() const { return m_dirty; }   // nodes to write this frame
        core::Range range(uint32_t node) const;
        bool structureChanged() const { return m_structureChanged; }
        uint64_t liveInstances() const { return m_allocator.used(); }

    private:
        struct Entry
        {
            core::Range range;
            uint64_t    markedFrame = 0;
        };

        void markDirty(uint32_t node);

        core::RangeAllocator  m_allocator;
        std::vector<Entry>    m_entries;   // indexed by node
        std::vector<uint32_t> m_dirty;
        uint64_t              m_frame = 0;
        bool                  m_structureChanged = false;
    };
}
