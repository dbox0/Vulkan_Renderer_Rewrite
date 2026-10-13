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

    void            write(uint32_t slot, const Material &material);
    const Material &read(uint32_t slot) const { return m_table.read(slot); }
    uint32_t        used() const { return 1 + static_cast<uint32_t>(m_slots.used()); }

    void                        setName(uint32_t slot, std::string_view name) { m_names[slot] = name; }
    std::string_view            name(uint32_t slot) const { return m_names[slot]; }
    std::span<const core::Range> ranges() const { return m_ranges; }

    void stage(gfx::FrameArena &arena) { m_table.stage(arena); }
    bool hasCopies() const { return m_table.hasCopies(); }
    void recordCopies(VkCommandBuffer cmd) const { m_table.recordCopies(cmd); }

    uint64_t flagsVersion() const { return m_flagsVersion; }

    VkBuffer        buffer() const  { return m_table.buffer(); }
    VkDeviceAddress address() const { return m_table.address(); }

private:
    uint64_t m_flagsVersion = 0;

    DeviceTable<Material>    m_table;
    core::RangeAllocator     m_slots{ MaxMaterials - 1 };
    std::vector<core::Range> m_ranges;
    std::vector<std::string> m_names;
};

}
