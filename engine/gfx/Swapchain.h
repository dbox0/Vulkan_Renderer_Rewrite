#pragma once
#include <cstdint>
#include <vector>

#include "Queue.h"
#include "Vk.h"

struct SDL_Window;

namespace gfx
{

class Context;

const char *presentModeName(VkPresentModeKHR mode);

// Swapchain images, their views, and one render-finished semaphore per image.
class Swapchain
{
public:
    static constexpr VkFormat Format = VK_FORMAT_B8G8R8A8_SRGB;

    explicit Swapchain(Context &ctx) : m_ctx(ctx) {}
    Swapchain(const Swapchain &) = delete;
    Swapchain &operator=(const Swapchain &) = delete;

    void create(SDL_Window *window);
    void destroy();

    // Waits for the GPU to go idle. Returns false while the window has no area.
    bool recreate();

    // False means "skip this frame": the swapchain is out of date and nothing was acquired.
    bool acquire(VkSemaphore imageAcquired, uint32_t &imageIndex);
    void present(const Queue &queue, uint32_t imageIndex);

    // Takes effect on the next frame (recreates the swapchain). Unsupported modes fall back to FIFO.
    void setPresentMode(VkPresentModeKHR mode);
    VkPresentModeKHR presentMode() const { return m_presentMode; }
    const std::vector<VkPresentModeKHR> &supportedPresentModes() const { return m_supportedPresentModes; }

    bool needsRecreate() const { return m_needsRecreate; }
    void flagForRecreate()     { m_needsRecreate = true; }

    VkExtent2D  extent() const                        { return m_extent; }
    uint32_t    imageCount() const                    { return static_cast<uint32_t>(m_images.size()); }
    VkImage     image(uint32_t index) const           { return m_images[index]; }
    VkImageView view(uint32_t index) const            { return m_views[index]; }
    VkSemaphore renderFinished(uint32_t index) const  { return m_renderFinished[index]; }

private:
    bool build(VkSwapchainKHR oldSwapchain);
    void destroyImageResources();

    Context    &m_ctx;
    SDL_Window *m_window = nullptr;

    VkSwapchainKHR           m_swapchain = VK_NULL_HANDLE;
    VkExtent2D               m_extent{};
    std::vector<VkImage>     m_images;
    std::vector<VkImageView> m_views;
    std::vector<VkSemaphore> m_renderFinished;
    bool                     m_needsRecreate = false;

    VkPresentModeKHR              m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
    std::vector<VkPresentModeKHR> m_supportedPresentModes;
};

}
