#pragma once
#include <cassert>
#include <cstdint>
#include <functional>
#include <deque>

namespace gfx {

    class DeletionQueue {
    public:

        void push(uint64_t safeAfter, std::function<void()> destroy) {
            assert(m_entries.empty() || m_entries.back().safeAfter <= safeAfter);
            m_entries.push_back({safeAfter, std::move(destroy)});
        }

        void collect(uint64_t completedValue) {
            while (!m_entries.empty() && m_entries.front().safeAfter <= completedValue) {
                m_entries.front().destroy();
                m_entries.pop_front();
            }
        }

        void flush() {
            for (Entry &entry : m_entries) {
                entry.destroy();
            }
            m_entries.clear();
        }

    private:
        struct Entry {
            uint64_t safeAfter;
            std::function<void()> destroy;
        };
        std::deque<Entry> m_entries;

    };

}