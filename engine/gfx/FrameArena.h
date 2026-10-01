#pragma once
#include <vulkan/vulkan_core.h>

#include "Resources.h"

namespace gfx {
    class Context;
}

struct ArenaAllocation {
    void           *cpu     = nullptr;
    VkBuffer        buffer  = VK_NULL_HANDLE;
    VkDeviceSize    offset  = 0;
    VkDeviceAddress address = 0;
};

class FrameArena {
public:
    void init(gfx::Context &ctx, VkDeviceSize capacity, const char *name);
    void destroy(gfx::Context &ctx);
    void reset();                                       // only after the slot's wait
    ArenaAllocation allocate(VkDeviceSize size, VkDeviceSize alignment = 16);
    void flush(const gfx::Context &ctx) const;               // flushes the used range once

private:
    gfx::Buffer       m_buffer;
    VkDeviceSize m_offset = 0;
    const char  *m_name   = "";
};