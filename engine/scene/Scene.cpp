#include "Scene.h"

void Scene::initialize(size_t maxNodes)
{
    m_nodeWorld.initialize(maxNodes);
    m_traversalStack.reserve(maxNodes);
}

void Scene::addRootNode(uint32_t nodeId)
{
    if (!nodeId) {
        return;
    }
    if (!m_rootNodeId) {
        m_rootNodeId = nodeId;
    } else {
        m_nodeWorld.getNode(m_lastRootNodeId).nextSiblingId = nodeId;
    }
    m_lastRootNodeId = nodeId;
}

void Scene::clear()
{
    m_nodeWorld.clear();
    m_rootNodeId = 0;
    m_lastRootNodeId = 0;
}

void Scene::collectDrawItems(std::vector<DrawItem> &out)
{
    out.clear();
    m_traversalStack.clear();

    for (uint32_t rootId = m_rootNodeId; rootId; rootId = m_nodeWorld.getNode(rootId).nextSiblingId) {
        m_traversalStack.push_back({ rootId, glm::mat4(1.0f) });
    }

    while (!m_traversalStack.empty()) {
        const auto [nodeId, parentTransform] = m_traversalStack.back();
        m_traversalStack.pop_back();

        Node &node = m_nodeWorld.getNode(nodeId);
        const glm::mat4 matWorld = parentTransform * node.getTransform();

        if (node.mesh.valid()) {
            out.push_back(DrawItem{ .mesh = node.mesh, .nodeId = nodeId, .worldMatrix = matWorld });
        }

        for (uint32_t childId = node.firstChildId; childId; childId = m_nodeWorld.getNode(childId).nextSiblingId) {
            m_traversalStack.push_back({ childId, matWorld });
        }
    }
}
