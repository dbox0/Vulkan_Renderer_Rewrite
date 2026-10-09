#pragma once
#include <cstdint>
#include "DeviceTable.h"
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

    class SceneExtract
    {
    public:
        static constexpr uint32_t MaxInstances = 256 * 1024;   // 16 MB at 64 bytes ea.

        SceneExtract(gfx::Context &ctx, const GeometryStore &geometry) : m_ctx(ctx), m_geometry(geometry) {}
        void init();
        void shutdown();

        void collect(const scene::Scene &scene);
        void write(const scene::Scene &scene, gfx::FrameArena &arena);
        void recordUploads(VkCommandBuffer cmd) const;

        VkDeviceAddress instancesAddress() const { return m_instances.address(); }
        std::span<const VkDrawIndexedIndirectCommand> commands() const { return m_commands; }
        uint32_t firstMirrored() const { return m_firstMirrored; }

        struct Stats { uint32_t written = 0; uint32_t copyRegions = 0; uint64_t live = 0; };
        Stats stats() const { return m_stats; }

    private:
        void rebuildCommands(const scene::Scene &scene);

        gfx::Context        &m_ctx;
        const GeometryStore &m_geometry;

        DeviceTable<Instance> m_instances;
        InstanceSlots m_slots{ MaxInstances };
        uint64_t      m_sceneEpoch = 0;

        std::vector<VkDrawIndexedIndirectCommand> m_commands;
        std::vector<VkDrawIndexedIndirectCommand> m_mirroredCommands;
        std::vector<uint8_t>      m_mirrored;
        std::vector<uint32_t>     m_pending;
        std::vector<uint8_t>      m_isPending;
        bool                      m_rebuildPending = false;

        uint32_t m_firstMirrored = 0;
        Stats    m_stats;
    };

}
