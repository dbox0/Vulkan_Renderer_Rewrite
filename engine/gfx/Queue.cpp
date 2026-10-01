#include "Queue.h"
#include <algorithm>
using namespace gfx;

void Queue::init(VkDevice device, uint32_t family)
{
    m_device = device;
    m_family = family;
    vkGetDeviceQueue(device, family, 0, &m_queue);

    const VkSemaphoreTypeCreateInfo timelineType
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = 0
    };
    const VkSemaphoreCreateInfo timelineInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &timelineType
    };
    VK_CHECK(vkCreateSemaphore(device, &timelineInfo, nullptr, &m_timeline));
}

uint64_t Queue::submit(const VkSubmitInfo2 &info) {
    constexpr uint32_t MaxSignals = 8;
    if (info.signalSemaphoreInfoCount >= MaxSignals) {
        core::fatal("Queue::submit: too many signal semaphores");
    }

    const uint64_t value = m_lastSubmitted + 1;
    std::array<VkSemaphoreSubmitInfo, MaxSignals> signals{};
    std::copy_n(info.pSignalSemaphoreInfos, info.signalSemaphoreInfoCount, signals.begin());

    signals[info.signalSemaphoreInfoCount] = VkSemaphoreSubmitInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = m_timeline,
        .value = value,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
    };

    VkSubmitInfo2 withTimeline = info;
    withTimeline.signalSemaphoreInfoCount = info.signalSemaphoreInfoCount + 1;
    withTimeline.pSignalSemaphoreInfos = signals.data();

    VK_CHECK(vkQueueSubmit2(m_queue, 1, &withTimeline, VK_NULL_HANDLE));
    m_lastSubmitted = value;
    return value;
}

uint64_t Queue::completed() {
    uint64_t value = 0;
    VK_CHECK(vkGetSemaphoreCounterValue(m_device, m_timeline, &value));
    return value;
}

VkResult Queue::present(const VkPresentInfoKHR &info) const
{
    return vkQueuePresentKHR(m_queue, &info);
}

void Queue::wait(uint64_t value) const
{
    const VkSemaphoreWaitInfo waitInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &m_timeline,
        .pValues = &value
    };
    VK_CHECK(vkWaitSemaphores(m_device, &waitInfo, UINT64_MAX));
}

void Queue::destroy() {
    vkDestroySemaphore(m_device, m_timeline, nullptr);
}