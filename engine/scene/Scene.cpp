#include "Scene.h"

#include <algorithm>
#include <format>

#include "core/Log.h"

namespace scene {
    void Scene::initialize(uint32_t capacity)
    {
        m_capacity = capacity;
        m_local.reserve(capacity);
        m_world.reserve(capacity);
        m_parent.reserve(capacity);
        m_generation.reserve(capacity);
        m_flags.reserve(capacity);
        m_names.reserve(capacity);
        m_changed.reserve(capacity);
        forEachTable([&](auto &table) { table.reserve(capacity); });
    }

    NodeHandle Scene::create(std::string_view name, NodeHandle parent) {
        if (parent.valid() && !alive(parent)) {
            core::warn(std::format("Scene::create: parent {{{}, {}}} is stale; '{}' not created",
                                   parent.index, parent.generation, name));
            return {};
        }
        if (m_count >= m_capacity) {
            if (!m_warnedFull) {
                core::warn(std::format("Scene is full ({} nodes); '{}' and later nodes not created", m_capacity, name));
                m_warnedFull = true;
            }
            return {};
        }

        const uint32_t index = m_count++;
        if (index == m_local.size()) {
            m_local.emplace_back();
            m_world.emplace_back(1.0f);
            m_parent.push_back(NoParent);
            m_generation.push_back(0);
            m_flags.push_back(0);
            m_names.emplace_back();
        }
        m_local[index]  = Transform{};
        m_world[index]  = glm::mat4(1.0f);
        m_parent[index] = parent.valid() ? parent.index : NoParent;
        m_flags[index]  = static_cast<uint8_t>(Alive | LocalDirty);
        m_names[index]  = name;

        m_firstDirty = std::min(m_firstDirty, index);
        return { index, m_generation[index] };
    }

    void Scene::destroy(NodeHandle node) {
        const uint32_t root = checked(node);

        // Descendants always have larger indices, so one forward pass finds the whole subtree.
        std::vector<uint8_t> doomed(m_count - root, 0);
        doomed[0] = 1;
        for (uint32_t i = root + 1; i < m_count; ++i) {
            const uint32_t p = m_parent[i];
            if ((m_flags[i] & Alive) && p != NoParent && p >= root && doomed[p - root]) {
                doomed[i - root] = 1;
            }
        }

        for (uint32_t i = root; i < m_count; ++i) {
            if (doomed[i - root]) {
                m_flags[i] = 0;
                ++m_generation[i];
                m_names[i].clear();
                forEachTable([i](auto &table) { table.remove(i); });
            }
        }
    }

    void Scene::clear() {
        for (uint32_t i = 0; i < m_count; ++i) {
            m_flags[i] = 0;
            ++m_generation[i];
        }
        m_count = 0;
        m_changed.clear();
        forEachTable([](auto &table) { table.clear(); });
        m_firstDirty = UINT32_MAX;
        m_warnedFull = false;
        ++m_version;
        ++m_epoch;
    }

    bool Scene::alive(NodeHandle node) const
    {
        return node.index < m_count
            && (m_flags[node.index] & Alive)
            && m_generation[node.index] == node.generation;
    }

    uint32_t Scene::checked(NodeHandle node) const {
        if (!alive(node)) {
            core::fatal(std::format("Scene: node handle {{{}, {}}} is stale", node.index, node.generation));
        }
        return node.index;
    }

    const Transform &Scene::local(NodeHandle node) const {
        return m_local[checked(node)];
    }

    void Scene::setLocal(NodeHandle node, const Transform &transform) {
        const uint32_t index = checked(node);
        m_local[index] = transform;
        m_flags[index] |= LocalDirty;
        m_firstDirty = std::min(m_firstDirty, index);
    }

    const glm::mat4 &Scene::world(NodeHandle node) const {
        return m_world[checked(node)];
    }

    NodeHandle Scene::parent(NodeHandle node) const {
        const uint32_t p = m_parent[checked(node)];
        return p == NoParent ? NodeHandle{} : NodeHandle{ p, m_generation[p] };
    }

    std::string_view Scene::name(NodeHandle node) const {
        return m_names[checked(node)];
    }

    void Scene::update() {
        forEachTable([](auto &table) { table.publish(); });
        updateTransforms();
    }

    void Scene::updateTransforms() {
        for (const uint32_t i : m_changed) {
            m_flags[i] &= ~WorldChanged;
        }
        m_changed.clear();
        if (m_firstDirty == UINT32_MAX) {
            return;
        }
        for (uint32_t i = m_firstDirty; i < m_count; ++i) {
            const uint8_t flags = m_flags[i];
            if (!(flags & Alive)) {
                continue;
            }
            const uint32_t p = m_parent[i];
            const bool parentMoved = p != NoParent && (m_flags[p] & WorldChanged);
            if (!(flags & LocalDirty) && !parentMoved) {
                continue;
            }
            m_world[i] = p == NoParent ? m_local[i].matrix() : m_world[p] * m_local[i].matrix();
            m_flags[i] = static_cast<uint8_t>((flags & ~LocalDirty) | WorldChanged);
            m_changed.push_back(i);
        }
        m_firstDirty = UINT32_MAX;
        ++m_version;
    }
}
