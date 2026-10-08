#include "RangeAllocator.h"
#include <format>
#include "Log.h"
#include <algorithm>
#include <iterator>

namespace core {
    RangeAllocator::RangeAllocator(uint64_t capacity) {
        m_capacity = capacity;
        m_used = 0;
        m_free.emplace(0,capacity);
    }

    void RangeAllocator::free(uint64_t offset, uint64_t count) {
        if (count == 0 ) return;

        const uint64_t end = offset + count;
        if (end > m_capacity || end < offset) {
            fatal(std::format("RangeAllocator::free: [{}, {}) is outside capacity {}", offset, end, m_capacity));
        }

        auto next = m_free.lower_bound(offset); // next free range
        auto prev = next != m_free.begin() ? std::prev(next) : m_free.end();

        if(prev != m_free.end() && prev->first + prev->second > offset){
           fatal(std::format("RangeAllocator::free: [{}, {}) overlaps free range [{}, {})",
               offset, end, prev->first, prev->first + prev->second));
        }
        if (next != m_free.end() && next->first < end) {
            fatal(std::format("RangeAllocator::free: [{}, {}) overlaps free range [{}, {})",
                              offset, end, next->first, next->first + next->second));
        }

        const bool touchesPrev = prev != m_free.end() && prev->first + prev->second == offset;
        const bool touchesNext = next != m_free.end() && next->first == end;


        if (touchesPrev && touchesNext) {
            prev->second += count + next->second;
            m_free.erase(next);
        } else if (touchesPrev) {
            prev->second += count;
        } else if (touchesNext) {
            const uint64_t merged = count + next->second;
            const auto after = m_free.erase(next);
            m_free.emplace_hint(after, offset, merged);
        } else {
            m_free.emplace_hint(next, offset, count);
        }

        m_used -= count;

    }

    uint64_t RangeAllocator::largestFree() const
    {
        uint64_t largest = 0;
        for (const auto &[offset, count] : m_free) {
            largest = std::max(largest, count);
        }
        return largest;
    }

    std::optional<uint64_t> RangeAllocator::allocate(uint64_t count) {
        if (count == 0) {
            return std::nullopt;
        }

        for (auto i = m_free.begin(); i != m_free.end(); ++i) {
            if (i->second >= count) {
                uint64_t offset = i->first;
                uint64_t size = i->second;

                m_free.erase(i);

                if (size > count) {
                    uint64_t remainingOffset = offset + count;
                    uint64_t remainingSize = size - count;
                    m_free[remainingOffset] = remainingSize;
                }
                m_used += count;
                return offset;
            }
        }
        return std::nullopt;
    }
}
