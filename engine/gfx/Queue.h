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
    void init(VkDevice device, uint32_t family);

    uint64_t submit(const VkSubmitInfo2 &info) ;

    VkResult present(const VkPresentInfoKHR &info) const;

    uint64_t completed();

    void destroy();
    void wait(uint64_t frameNumber) const;
    VkQueue  handle() const { return m_queue; }
    uint32_t family() const { return m_family; }
    uint64_t lastSubmitted() const { return m_lastSubmitted; }

    VkSemaphore timeline() const {return m_timeline;}

private:
    VkQueue  m_queue  = VK_NULL_HANDLE;
    uint32_t m_family = UINT32_MAX;
    VkDevice m_device = VK_NULL_HANDLE;
    VkSemaphore m_timeline = 0;
    uint64_t m_lastSubmitted = 0;
};

}
