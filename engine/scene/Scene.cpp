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

void Scene::collectDrawItems(std::vector<DrawItem> &out)
{
    out.clear();
    m_traversalStack.clear();

    uint32_t nodeId = m_rootNodeId;
    while (nodeId) {
        Node &node = m_nodeWorld.getNode(nodeId);
        m_traversalStack.push_back({ &node, glm::mat4(1.0f) });
        nodeId = node.nextSiblingId;
    }

    while (!m_traversalStack.empty()) {
        auto [node, parentTransform] = m_traversalStack.back();
        m_traversalStack.pop_back();

        const glm::mat4 matWorld = parentTransform * node->getTransform();

        if (node->meshId) {
            out.push_back(DrawItem{ node->meshId, matWorld });
        }

        uint32_t childNodeId = node->firstChildId;
        while (childNodeId) {
            Node &child = m_nodeWorld.getNode(childNodeId);
            m_traversalStack.push_back({ &child, matWorld });
            childNodeId = child.nextSiblingId;
        }
    }
}
