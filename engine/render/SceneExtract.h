#pragma once
#include <cstdint>
#include <glm/vec3.hpp>
#include "DeviceTable.h"
#include "DrawSort.h"
#include "InstanceSlots.h"
#include "shared/GpuTypes.h"
#include "gfx/Resources.h"
#include "gfx/Vk.h"
#include <span>
#include <vector>

class GeometryStore;

namespace scene {
    class Scene;
}

namespace gfx {
    class FrameArena;
    class Context;
}

namespace render
{

    class MaterialTable;

    class SceneExtract
    {
    public:
        static constexpr uint32_t MaxInstances = 256 * 1024;   // 16 MB at 64 bytes ea.

        SceneExtract(gfx::Context &ctx, const GeometryStore &geometry, const MaterialTable &materials)
            : m_ctx(ctx), m_geometry(geometry), m_materials(materials) {}
        void init();
        void shutdown();

        void collect(const scene::Scene &scene);
        void write(const scene::Scene &scene, gfx::FrameArena &arena);
        bool hasCopies() const { return m_instances.hasCopies(); }
        void recordCopies(VkCommandBuffer cmd) const { m_instances.recordCopies(cmd); }

        VkBuffer        instanceBuffer() const   { return m_instances.buffer(); }
        VkDeviceAddress instancesAddress() const { return m_instances.address(); }
        std::span<const DrawRecord> records() const { return m_records; }
        std::span<const glm::vec3>  centers() const { return m_centers; }

        struct Stats { uint32_t written = 0; uint32_t copyRegions = 0; uint64_t live = 0; };
        Stats stats() const { return m_stats; }

    private:
        void rebuildRecords(const scene::Scene &scene);

        gfx::Context        &m_ctx;
        const GeometryStore &m_geometry;
        const MaterialTable &m_materials;

        DeviceTable<Instance> m_instances;
        InstanceSlots m_slots{ MaxInstances };
        uint64_t      m_sceneEpoch = 0;

        std::vector<DrawRecord>   m_records;
        std::vector<glm::vec3>    m_centers;   // world AABB center per instance slot
        std::vector<uint8_t>      m_mirrored;
        std::vector<uint32_t>     m_pending;
        std::vector<uint8_t>      m_isPending;
        bool                      m_rebuildPending = false;
        uint64_t                  m_materialFlagsVersion = UINT64_MAX;

        Stats    m_stats;
    };

}
