#pragma once
#include <cstdint>

#include "Vk.h"

namespace gfx
{

// The only place the engine submits to or presents on the queue.
// A mutex or a second (async compute) queue goes here
class Queue
{
public:
    void init(VkDevice device, uint32_t family)
    {
        m_family = family;
        vkGetDeviceQueue(device, family, 0, &m_queue);
    }

    void submit(const VkSubmitInfo2 &info) const
    {
        VK_CHECK(vkQueueSubmit2(m_queue, 1, &info, VK_NULL_HANDLE));
    }

    VkResult present(const VkPresentInfoKHR &info) const
    {
        return vkQueuePresentKHR(m_queue, &info);
    }

    void waitIdle() const
    {
        VK_CHECK(vkQueueWaitIdle(m_queue));
    }

    VkQueue  handle() const { return m_queue; }
    uint32_t family() const { return m_family; }

private:
    VkQueue  m_queue  = VK_NULL_HANDLE;
    uint32_t m_family = UINT32_MAX;
};

}
