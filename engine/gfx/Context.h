#pragma once
#include <cstdint>
#include <filesystem>
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

    // cacheDir holds pipelines.bin
    // empty disables the pipeline cache file.
    void init(SDL_Window *window, std::filesystem::path cacheDir);
    void shutdown();

    VkInstance       instance() const       { return m_instance; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkDevice         device() const         { return m_device; }
    VkSurfaceKHR     surface() const        { return m_surface; }
    const Queue     &queue() const          { return m_queue; }
    Queue           &queue()                { return m_queue; }
    uint32_t         queueFamily() const    { return m_queueFamily; }
    VmaAllocator     allocator() const      { return m_allocator; }
    VkPipelineCache  pipelineCache() const  { return m_pipelineCache; }

    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, MemoryIntent intent, const char *name) const;
    void   invalidate(const Buffer &buffer) const;
    void   destroyBuffer(Buffer &buffer) const;

    // Copies into a host-visible buffer (flushes if the memory needs it).
    void write(const Buffer &dst, const void *data, VkDeviceSize size, VkDeviceSize dstOffset = 0) const;

    // Flushes CPU writes to a host-visible buffer; a no-op on coherent memory.
    void flush(const Buffer &buffer) const;
    
    Image createImage(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, const char *name) const;
    void  destroyImage(Image &image) const;
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
    void createPipelineCache();
    void savePipelineCache() const;
    void setObjectName(VkObjectType type, uint64_t handle, const char *name) const;

    VkInstance               m_instance       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR             m_surface        = VK_NULL_HANDLE;
    VkPhysicalDevice         m_physicalDevice = VK_NULL_HANDLE;
    VkDevice                 m_device         = VK_NULL_HANDLE;
    Queue                    m_queue;
    uint32_t                 m_queueFamily    = UINT32_MAX;
    VmaAllocator             m_allocator      = nullptr;
    DeletionQueue            m_deletionQueue;

    std::filesystem::path      m_cacheDir;
    VkPipelineCache            m_pipelineCache = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties m_properties{};
};

}
