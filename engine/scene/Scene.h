#pragma once
#include <cstdint>
#include <vector>
#include <glm/mat4x4.hpp>

#include "NodeWorld.h"

// A node with a mesh at its world transform. The renderer expands it into one draw per submesh.
struct DrawItem
{
    uint32_t  meshId = 0;
    glm::mat4 worldMatrix{1.0f};
};

class Scene
{
public:
    void initialize(size_t maxNodes);

    NodeWorld &nodes()             { return m_nodeWorld; }
    Node &getNode(uint32_t nodeId) { return m_nodeWorld.getNode(nodeId); }
    uint32_t rootNodeId() const    { return m_rootNodeId; }
    size_t maxNodes() const        { return m_nodeWorld.maxNodes(); }

    void addRootNode(uint32_t nodeId);

    void collectDrawItems(std::vector<DrawItem> &out);

private:
    NodeWorld m_nodeWorld;
    uint32_t  m_rootNodeId     = 0;
    uint32_t  m_lastRootNodeId = 0;

    // Kept as a member so traversal doesn't reallocate every frame.
    std::vector<std::pair<Node *, glm::mat4>> m_traversalStack;
};
