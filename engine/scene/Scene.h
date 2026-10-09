#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include <glm/mat4x4.hpp>

#include "ComponentTable.h"
#include "Components.h"
#include "SceneTypes.h"
#include "core/Handle.h"

namespace scene {
    struct DrawItem
    {
        MeshHandle mesh;
        uint32_t   nodeId = 0;
        glm::mat4  worldMatrix{1.0f};
    };

    class Scene
    {
    public:
        void initialize(uint32_t capacity);

        NodeHandle create(std::string_view name, NodeHandle parent = {});
        void       destroy(NodeHandle node);   // the node and its whole subtree
        void       clear();

        bool              alive(NodeHandle node) const;
        const Transform  &local(NodeHandle node) const;
        void              setLocal(NodeHandle node, const Transform &transform);
        const glm::mat4  &world(NodeHandle node) const;
        NodeHandle        parent(NodeHandle node) const;
        std::string_view  name(NodeHandle node) const;

        // publishes component change logs, then transforms once per frame, before rendering.
        void update();

        std::span<const uint32_t>  changedNodes() const { return m_changed; }
        std::span<const glm::mat4> worlds() const { return { m_world.data(), m_count }; }
        uint64_t version() const { return m_version; }
        uint64_t epoch() const { return m_epoch; }
        uint32_t indexCount() const { return m_count; }   // slots in use, dead ones included
        uint32_t capacity() const { return m_capacity; }

        template <typename T>
        T &setComponent(NodeHandle node, T component) { return table<T>().set(checked(node), std::move(component)); }

        template <typename T>
        void removeComponent(NodeHandle node) { table<T>().remove(checked(node)); }

        template <typename T>
        const T *findComponent(NodeHandle node) const { return components<T>().find(checked(node)); }

        template <typename T>
        const ComponentTable<T> &components() const { return std::get<ComponentTable<T>>(m_components); }

        void collectDrawItems(std::vector<DrawItem> &out) const;

    private:
        enum Flag : uint8_t { Alive = 1, LocalDirty = 2, WorldChanged = 4 };

         using Components = std::tuple<
            ComponentTable<MeshRenderer>,
            ComponentTable<Light>,
            ComponentTable<CameraComponent>>;

        template <typename T>
        ComponentTable<T> &table() { return std::get<ComponentTable<T>>(m_components); }

        template <typename Fn>
        void forEachTable(Fn &&fn) { std::apply([&](auto &...tables) { (fn(tables), ...); }, m_components); }

        uint32_t checked(NodeHandle node) const;
        void     updateTransforms();

        std::vector<Transform>   m_local;
        std::vector<glm::mat4>   m_world;
        std::vector<uint32_t>    m_parent;
        std::vector<uint32_t>    m_generation;
        std::vector<uint8_t>     m_flags;
        std::vector<std::string> m_names;
        Components               m_components;

        std::vector<uint32_t> m_changed;
        uint32_t m_count      = 0;
        uint32_t m_capacity   = 0;
        uint32_t m_firstDirty = UINT32_MAX;
        uint64_t m_version    = 0;
        uint64_t m_epoch      = 0;
        bool     m_warnedFull = false;
    };
}
