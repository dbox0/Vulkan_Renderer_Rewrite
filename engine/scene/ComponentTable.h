#pragma once
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace scene {
    // Sparse set keyed by node index. Dense order changes on remove, so nothing may depend on it.
    template <typename T>
    class ComponentTable
    {
    public:
        void reserve(uint32_t indexCapacity) { m_sparse.reserve(indexCapacity); }

        T &set(uint32_t node, T value)
        {
            if (node >= m_sparse.size()) {
                m_sparse.resize(node + 1, None);
            }
            m_pendingLog.push_back(node);

            if (m_sparse[node] != None) {
                T &existing = m_values[m_sparse[node]];
                existing = std::move(value);
                return existing;
            }
            m_sparse[node] = size();
            m_nodes.push_back(node);
            m_values.push_back(std::move(value));
            return m_values.back();
        }

        void remove(uint32_t node)
        {
            if (!has(node)) {
                return;
            }
            const uint32_t slot = m_sparse[node];
            const uint32_t last = size() - 1;

            // When slot == last, the fix-up line writes m_sparse[node], and the next line must overwrite it.
            m_nodes[slot]  = m_nodes[last];
            m_values[slot] = std::move(m_values[last]);
            m_sparse[m_nodes[slot]] = slot;
            m_sparse[node] = None;

            m_nodes.pop_back();
            m_values.pop_back();
            m_pendingLog.push_back(node);
        }

        bool has(uint32_t node) const { return node < m_sparse.size() && m_sparse[node] != None; }
        T       *find(uint32_t node)       { return has(node) ? &m_values[m_sparse[node]] : nullptr; }
        const T *find(uint32_t node) const { return has(node) ? &m_values[m_sparse[node]] : nullptr; }

        void clear()
        {
            for (const uint32_t node : m_nodes) {
                m_sparse[node] = None;
            }
            m_nodes.clear();
            m_values.clear();
            m_pendingLog.clear();
            m_frameLog.clear();
        }

        std::span<const uint32_t> nodes() const  { return m_nodes; }
        std::span<const T>        values() const { return m_values; }
        uint32_t size() const { return static_cast<uint32_t>(m_nodes.size()); }

        // Nodes whose component was set or removed. publish() hands the edits made since the last
        // publish to this frame, so edits made before the scene update are visible to this frame's render.
        void publish() { m_frameLog.swap(m_pendingLog); m_pendingLog.clear(); }
        std::span<const uint32_t> touched() const { return m_frameLog; }

    private:
        static constexpr uint32_t None = UINT32_MAX;

        std::vector<uint32_t> m_sparse;
        std::vector<uint32_t> m_nodes;
        std::vector<T>        m_values;
        std::vector<uint32_t> m_pendingLog;
        std::vector<uint32_t> m_frameLog;
    };
}
