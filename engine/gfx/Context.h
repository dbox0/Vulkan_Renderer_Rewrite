#pragma once
#include <cstdint>
#include <functional>

#include "Queue.h"
#include "Resources.h"
#include "Vk.h"
#include "DeletionQueue.h"

struct SDL_Window;

namespace gfx
{

class Context
{
public:
    static constexpr uint32_t ApiVersion = VK_API_VERSION_1_3;

    Context() = default;
    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;

    void init(SDL_Window *window);
    void shutdown();

    VkInstance       instance() const       { return m_instance; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkDevice         device() const         { return m_device; }
    VkSurfaceKHR     surface() const        { return m_surface; }
    const Queue     &queue() const          { return m_queue; }
    Queue           &queue()                { return m_queue; }
    uint32_t         queueFamily() const    { return m_queueFamily; }
    VmaAllocator     allocator() const      { return m_allocator; }

    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, MemoryIntent intent, const char *name) const;
    void   invalidate(const Buffer &buffer) const;
    void   destroyBuffer(Buffer &buffer) const;

    // Copies into a host-visible buffer (flushes if the memory needs it).
    void write(const Buffer &dst, const void *data, VkDeviceSize size, VkDeviceSize dstOffset = 0) const;

    // Flushes CPU writes to a host-visible buffer; a no-op on coherent memory.
    void flush(const Buffer &buffer) const;

    // Copies into any buffer through a staging buffer. Blocks until the GPU is done.
    void upload(const Buffer &dst, const void *data, VkDeviceSize size, VkDeviceSize dstOffset = 0);

    Image createImage(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, const char *name) const;
    void  destroyImage(Image &image) const;

    // Records an sRGB RGBA8 texture upload into cmd. outStaging must be destroyed after the submit.
    void createImage2D(VkCommandBuffer cmd, const unsigned char *pixels, uint32_t width, uint32_t height,
                       Image &outImage, Buffer &outStaging, const char *name) const;

    // Records, submits and waits. Load-time only.
    void immediateSubmit(const std::function<void(VkCommandBuffer)> &record);

    template <typename Handle>
    void setName(VkObjectType type, Handle handle, const char *name) const
    {
        setObjectName(type, reinterpret_cast<uint64_t>(handle), name);
    }

    void retire(Buffer buffer);
    void retire(Image image);
    void retire(std::function<void()> destroy);
    void retireAt(uint64_t safeAfter, std::function<void()> fn);
    void collect();


private:
    void createInstance();
    void createDebugMessenger();
    void pickPhysicalDevice();
    void createDevice();
    void createAllocator();
    void setObjectName(VkObjectType type, uint64_t handle, const char *name) const;

    VkInstance               m_instance       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR             m_surface        = VK_NULL_HANDLE;
    VkPhysicalDevice         m_physicalDevice = VK_NULL_HANDLE;
    VkDevice                 m_device         = VK_NULL_HANDLE;
    Queue                    m_queue;
    uint32_t                 m_queueFamily    = UINT32_MAX;
    VmaAllocator             m_allocator      = nullptr;
    VkCommandPool            m_immediatePool  = VK_NULL_HANDLE;
    DeletionQueue            m_deletionQueue;
};

}
