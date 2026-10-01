#pragma once
#include <cstdint>
#include <format>
#include <utility>
#include <vector>

#include "Node.h"
#include "core/Log.h"

class NodeWorld {

    std::vector<Node> m_nodes;
    size_t m_maxNodes = 0;

public:
    void initialize(const size_t maxNodes) {
        m_maxNodes = maxNodes;
        m_nodes.reserve(m_maxNodes);
    }
    [[nodiscard]] size_t maxNodes() const {return m_maxNodes;}
    void clear() { m_nodes.clear(); }

    // A runtime check rather than an assert: growing past the reservation reallocates
    // and invalidates every Node& handed out so far.
    std::pair<Node &, uint32_t> createNode() {
        if (m_nodes.size() >= m_maxNodes) {
            core::fatal(std::format("NodeWorld is full ({} nodes); raise MaxNodes", m_maxNodes));
        }
        m_nodes.push_back(Node{});
        const auto nodeId = static_cast<uint32_t>(m_nodes.size());
        return {m_nodes[nodeId - 1], nodeId};
    }

    Node &getNode(uint32_t nodeId) {
        if (nodeId == 0) {
            core::fatal("Tried retrieving node with nil ID");
        }
        return m_nodes[nodeId - 1];
    }
};
