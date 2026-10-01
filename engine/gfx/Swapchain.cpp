#include "Swapchain.h"

#include <SDL3/SDL_video.h>

#include <algorithm>

#include "Context.h"

namespace gfx
{

void Swapchain::create(SDL_Window *window)
{
    m_window = window;
    if (!build(VK_NULL_HANDLE)) {
        core::fatal("Cannot create a swapchain for a window with no area");
    }
}

void Swapchain::destroy()
{
    destroyImageResources();
    if (m_swapchain) {
        vkDestroySwapchainKHR(m_ctx.device(), m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

bool Swapchain::recreate()
{
    VK_CHECK(vkDeviceWaitIdle(m_ctx.device()));
    destroyImageResources();

    const VkSwapchainKHR oldSwapchain = m_swapchain;
    m_swapchain = VK_NULL_HANDLE;
    const bool built = build(oldSwapchain);
    vkDestroySwapchainKHR(m_ctx.device(), oldSwapchain, nullptr);

    m_needsRecreate = !built;
    return built;
}

bool Swapchain::build(VkSwapchainKHR oldSwapchain)
{
    VkSurfaceCapabilitiesKHR caps{};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_ctx.physicalDevice(), m_ctx.surface(), &caps));

    // UINT32_MAX means the surface takes its size from the swapchain (Wayland), so use the window's pixel size.
    m_extent = caps.currentExtent;
    if (m_extent.width == UINT32_MAX) {
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(m_window, &width, &height);
        m_extent.width  = std::clamp(static_cast<uint32_t>(width),  caps.minImageExtent.width,  caps.maxImageExtent.width);
        m_extent.height = std::clamp(static_cast<uint32_t>(height), caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (m_extent.width == 0 || m_extent.height == 0) {
        return false;
    }

    uint32_t formatCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(m_ctx.physicalDevice(), m_ctx.surface(), &formatCount, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(m_ctx.physicalDevice(), m_ctx.surface(), &formatCount, formats.data()));
    const bool formatSupported = std::ranges::any_of(formats, [](const VkSurfaceFormatKHR &f) {
        return f.format == Format && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (!formatSupported) {
        core::fatal("The surface does not support B8G8R8A8_SRGB");
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) {
        imageCount = std::min(imageCount, caps.maxImageCount);
    }

    const VkSwapchainCreateInfoKHR createInfo
    {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = m_ctx.surface(),
        .minImageCount = imageCount,
        .imageFormat = Format,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = m_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = oldSwapchain
    };
    VK_CHECK(vkCreateSwapchainKHR(m_ctx.device(), &createInfo, nullptr, &m_swapchain));

    VK_CHECK(vkGetSwapchainImagesKHR(m_ctx.device(), m_swapchain, &imageCount, nullptr));
    m_images.resize(imageCount);
    VK_CHECK(vkGetSwapchainImagesKHR(m_ctx.device(), m_swapchain, &imageCount, m_images.data()));

    m_views.resize(imageCount);
    m_renderFinished.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        const VkImageViewCreateInfo viewInfo
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = Format,
            .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
        };
        VK_CHECK(vkCreateImageView(m_ctx.device(), &viewInfo, nullptr, &m_views[i]));

        // Per image, not per frame in flight: presentation of image i waits on semaphore i.
        const VkSemaphoreCreateInfo semaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VK_CHECK(vkCreateSemaphore(m_ctx.device(), &semaphoreInfo, nullptr, &m_renderFinished[i]));

        m_ctx.setName(VK_OBJECT_TYPE_IMAGE, m_images[i], "swapchain image");
        m_ctx.setName(VK_OBJECT_TYPE_SEMAPHORE, m_renderFinished[i], "render finished");
    }
    return true;
}

void Swapchain::destroyImageResources()
{
    for (VkImageView view : m_views) {
        vkDestroyImageView(m_ctx.device(), view, nullptr);
    }
    for (VkSemaphore semaphore : m_renderFinished) {
        vkDestroySemaphore(m_ctx.device(), semaphore, nullptr);
    }
    m_views.clear();
    m_renderFinished.clear();
    m_images.clear();
}

bool Swapchain::acquire(VkSemaphore imageAcquired, uint32_t &imageIndex)
{
    const VkResult result = vkAcquireNextImageKHR(m_ctx.device(), m_swapchain, UINT64_MAX,
                                                  imageAcquired, VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        m_needsRecreate = true;
        return false;
    }
    if (result == VK_SUBOPTIMAL_KHR) {
        m_needsRecreate = true;   // still usable for this frame
        return true;
    }
    VK_CHECK(result);
    return true;
}

void Swapchain::present(VkQueue queue, uint32_t imageIndex)
{
    const VkSemaphore waitSemaphore = m_renderFinished[imageIndex];
    const VkPresentInfoKHR presentInfo
    {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &waitSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &m_swapchain,
        .pImageIndices = &imageIndex
    };
    const VkResult result = vkQueuePresentKHR(queue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        m_needsRecreate = true;
        return;
    }
    VK_CHECK(result);
}

} // namespace gfx
