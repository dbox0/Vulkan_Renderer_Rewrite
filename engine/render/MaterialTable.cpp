#include "MaterialTable.h"

namespace render
{

void MaterialTable::init(gfx::Context &ctx, const Material &defaultMaterial)
{
    m_table.init(ctx, MaxMaterials, "materials");
    m_table.write(0, defaultMaterial);
    m_names.assign(MaxMaterials, {});
    m_names[0] = "Default";
}

void MaterialTable::destroy(gfx::Context &ctx)
{
    m_table.destroy(ctx);
    m_ranges.clear();
    m_names.clear();
}

std::optional<core::Range> MaterialTable::allocate(uint32_t count)
{
    if (count == 0) {
        return core::Range{};
    }
    const std::optional<uint64_t> offset = m_slots.allocate(count);
    if (!offset) {
        return std::nullopt;
    }
    const core::Range range{ .offset = *offset + 1, .count = count };
    m_ranges.push_back(range);
    return range;
}

void MaterialTable::releaseAll()
{
    m_slots = core::RangeAllocator(MaxMaterials - 1);
    m_ranges.clear();
}

}
