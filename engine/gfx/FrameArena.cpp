#include "FrameArena.h"

#include "Context.h"

namespace gfx {
    ArenaAllocation FrameArena::allocate(VkDeviceSize size, VkDeviceSize alignment) {
        const VkDeviceSize offset = (m_offset + alignment - 1) & ~(alignment - 1);
        if (offset + size > m_buffer.size) {
            core::fatal(std::format("Frame arena '{}' is full ({} bytes)", m_name, m_buffer.size));
        }
        m_offset = offset + size;
        return { static_cast<std::byte *>(m_buffer.mapped) + offset, m_buffer.buffer, offset, m_buffer.address + offset };

    }
    void FrameArena::init(gfx::Context &ctx, VkDeviceSize capacity, const char* name) {
        m_name = name;
        m_buffer = ctx.createBuffer(capacity,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, gfx::MemoryIntent::Upload, name);
    }
    void FrameArena::destroy(gfx::Context &ctx) {
        ctx.destroyBuffer(m_buffer);
    }
    void FrameArena::flush(const gfx::Context &ctx) const {
        if (m_offset > 0) {
            VK_CHECK(vmaFlushAllocation(ctx.allocator(),m_buffer.allocation,0,m_offset));
        }
    }
}
