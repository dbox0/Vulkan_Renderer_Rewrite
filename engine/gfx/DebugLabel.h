#pragma once
#include "Vk.h"

namespace gfx
{

// Names a range of commands in RenderDoc and in validation messages.
class DebugLabel
{
public:
    DebugLabel(VkCommandBuffer cmd, const char *name) : m_cmd(cmd)
    {
        const VkDebugUtilsLabelEXT label
        {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            .pLabelName = name
        };
        vkCmdBeginDebugUtilsLabelEXT(m_cmd, &label);
    }

    ~DebugLabel() { vkCmdEndDebugUtilsLabelEXT(m_cmd); }

    DebugLabel(const DebugLabel &) = delete;
    DebugLabel &operator=(const DebugLabel &) = delete;

private:
    VkCommandBuffer m_cmd;
};

}
