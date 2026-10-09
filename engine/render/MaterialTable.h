#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "DeviceTable.h"
#include "core/RangeAllocator.h"
#include "shared/GpuTypes.h"

namespace render
{

class MaterialTable
{
public:
    static constexpr uint32_t MaxMaterials = 4096;

    void init(gfx::Context &ctx, const Material &defaultMaterial);
    void destroy(gfx::Context &ctx);

    std::optional<core::Range> allocate(uint32_t count);
    void releaseAll();

    void            write(uint32_t slot, const Material &material) { m_table.write(slot, material); }
    const Material &read(uint32_t slot) const { return m_table.read(slot); }
    uint32_t        used() const { return 1 + static_cast<uint32_t>(m_slots.used()); }

    void                        setName(uint32_t slot, std::string_view name) { m_names[slot] = name; }
    std::string_view            name(uint32_t slot) const { return m_names[slot]; }
    std::span<const core::Range> ranges() const { return m_ranges; }

    void stage(gfx::FrameArena &arena) { m_table.stage(arena); }
    void recordUploads(VkCommandBuffer cmd) const { m_table.recordUploads(cmd, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT); }
    VkDeviceAddress address() const { return m_table.address(); }

private:
    DeviceTable<Material>    m_table;
    core::RangeAllocator     m_slots{ MaxMaterials - 1 };
    std::vector<core::Range> m_ranges;
    std::vector<std::string> m_names;
};

}
