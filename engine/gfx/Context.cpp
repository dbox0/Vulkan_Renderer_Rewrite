#include "Context.h"

#include <SDL3/SDL_vulkan.h>

#include "Barriers.h"
#include "DebugLabel.h"

#include <iterator>
#include <vector>

namespace gfx
{

#ifndef NDEBUG
constexpr bool EnableValidation = true;
#else
constexpr bool EnableValidation = false;
#endif

namespace
{

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT *data,
                                             void *)
{
    const char *level = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "error" : "warn";
    std::fprintf(stderr, "[vk %s] %s\n", level, data->pMessage);
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT messengerInfo()
{
    return VkDebugUtilsMessengerCreateInfoEXT
    {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback
    };
}

bool isDepthFormat(VkFormat format)
{
    return format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT ||
           format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

}

void Context::init(SDL_Window *window)
{
    VK_CHECK(volkInitialize());
    createInstance();
    createDebugMessenger();

    if (!SDL_Vulkan_CreateSurface(window, m_instance, nullptr, &m_surface)) {
        core::fatal(std::format("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError()));
    }

    pickPhysicalDevice();
    createDevice();
    createAllocator();

    const VkCommandPoolCreateInfo poolInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = m_queueFamily
    };
    VK_CHECK(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_immediatePool));
}

void Context::shutdown()
{
    if (m_immediatePool) {
        vkDestroyCommandPool(m_device, m_immediatePool, nullptr);
    }
    if (m_allocator) {
        vmaDestroyAllocator(m_allocator);
    }
    if (!m_queue.handle() ) {
        m_queue.destroy();
    }
    if (m_device) {
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_surface) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
    if (m_debugMessenger) {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
    }
    if (m_instance) {
        vkDestroyInstance(m_instance, nullptr);
    }
    volkFinalize();
}

void Context::createInstance()
{
    const VkApplicationInfo appInfo
    {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Vulkan Renderer",
        .apiVersion = ApiVersion
    };

    uint32_t sdlExtensionCount = 0;
    const char *const *sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);

    std::vector<const char *> extensions(sdlExtensions, sdlExtensions + sdlExtensionCount);
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    std::vector<const char *> layers;
    if (EnableValidation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    // Synchronization validation catches missing barriers; it is off by default.
    const VkBool32 enableSyncValidation = VK_TRUE;
    const VkLayerSettingEXT layerSettings[]
    {
        { "VK_LAYER_KHRONOS_validation", "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &enableSyncValidation }
    };

    VkDebugUtilsMessengerCreateInfoEXT debugInfo = messengerInfo();
    const VkLayerSettingsCreateInfoEXT layerSettingsInfo
    {
        .sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
        .pNext = &debugInfo,
        .settingCount = static_cast<uint32_t>(std::size(layerSettings)),
        .pSettings = layerSettings
    };

    const VkInstanceCreateInfo createInfo
    {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = EnableValidation ? &layerSettingsInfo : nullptr,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data()
    };

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_instance));
    volkLoadInstance(m_instance);
}

void Context::createDebugMessenger()
{
    if (!EnableValidation) {
        return;
    }
    const VkDebugUtilsMessengerCreateInfoEXT info = messengerInfo();
    VK_CHECK(vkCreateDebugUtilsMessengerEXT(m_instance, &info, nullptr, &m_debugMessenger));
}

void Context::pickPhysicalDevice()
{
    uint32_t count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &count, nullptr));
    if (count == 0) {
        core::fatal("No Vulkan device found");
    }
    std::vector<VkPhysicalDevice> devices(count);
    VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &count, devices.data()));

    // First discrete GPU, otherwise the first device.
    m_physicalDevice = devices[0];
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            m_physicalDevice = device;
            break;
        }
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    core::log(std::format("GPU: {}", props.deviceName));

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &familyCount, families.data());

    for (uint32_t i = 0; i < familyCount; ++i) {
        VkBool32 canPresent = VK_FALSE;
        VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &canPresent));
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && canPresent) {
            m_queueFamily = i;
            break;
        }
    }
    if (m_queueFamily == UINT32_MAX) {
        core::fatal("No queue family supports graphics, compute and present");
    }
}

void Context::createDevice()
{
    VkPhysicalDeviceVulkan13Features supported13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceVulkan12Features supported12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &supported13 };
    VkPhysicalDeviceVulkan11Features supported11{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, .pNext = &supported12 };
    VkPhysicalDeviceFeatures2 supported{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &supported11 };
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &supported);

    if (!supported13.dynamicRendering || !supported13.synchronization2 ||
        !supported12.timelineSemaphore || !supported12.bufferDeviceAddress ||
        !supported12.scalarBlockLayout || !supported12.drawIndirectCount ||
        !supported12.descriptorIndexing || !supported12.runtimeDescriptorArray ||
        !supported12.descriptorBindingPartiallyBound ||
        !supported12.descriptorBindingSampledImageUpdateAfterBind ||
        !supported12.shaderSampledImageArrayNonUniformIndexing ||
        !supported11.shaderDrawParameters ||
        !supported.features.multiDrawIndirect || !supported.features.shaderInt64)
    {
        core::fatal("The GPU is missing a required Vulkan 1.3 feature");
    }

    VkPhysicalDeviceVulkan13Features features13
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE
    };
    VkPhysicalDeviceVulkan12Features features12
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &features13,
        .drawIndirectCount = VK_TRUE,
        .descriptorIndexing = VK_TRUE,
        .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
        .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
        .descriptorBindingPartiallyBound = VK_TRUE,
        .runtimeDescriptorArray = VK_TRUE,
        .scalarBlockLayout = VK_TRUE,
        .timelineSemaphore = VK_TRUE,
        .bufferDeviceAddress = VK_TRUE
    };
    VkPhysicalDeviceVulkan11Features features11
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &features12,
        .shaderDrawParameters = VK_TRUE
    };
    VkPhysicalDeviceFeatures2 features
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &features11,
        .features
        {
            .multiDrawIndirect = VK_TRUE,
            .shaderInt64 = VK_TRUE
        }
    };

    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queueInfo
    {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = m_queueFamily,
        .queueCount = 1,
        .pQueuePriorities = &priority
    };

    const char *extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    const VkDeviceCreateInfo createInfo
    {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
        .enabledExtensionCount = static_cast<uint32_t>(std::size(extensions)),
        .ppEnabledExtensionNames = extensions
    };

    VK_CHECK(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device));
    volkLoadDevice(m_device);
    m_queue.init(m_device, m_queueFamily);
    setName(VK_OBJECT_TYPE_SEMAPHORE, m_queue.timeline(), "queue timeline");
}

void Context::createAllocator()
{
    VmaVulkanFunctions functions{};
    const VmaAllocatorCreateInfo createInfo
    {
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = m_physicalDevice,
        .device = m_device,
        .pVulkanFunctions = &functions,
        .instance = m_instance,
        .vulkanApiVersion = ApiVersion
    };
    VK_CHECK(vmaImportVulkanFunctionsFromVolk(&createInfo, &functions));
    VK_CHECK(vmaCreateAllocator(&createInfo, &m_allocator));
}

void Context::setObjectName(VkObjectType type, uint64_t handle, const char *name) const
{
    const VkDebugUtilsObjectNameInfoEXT info
    {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = type,
        .objectHandle = handle,
        .pObjectName = name
    };
    vkSetDebugUtilsObjectNameEXT(m_device, &info);
}

Buffer Context::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible, const char *name) const
{
    const VkBufferCreateInfo bufferInfo
    {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage
    };
    const VmaAllocationCreateInfo allocInfo
    {
        .flags = hostVisible ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
                             : VmaAllocationCreateFlags{0},
        .usage = VMA_MEMORY_USAGE_AUTO
    };

    Buffer buffer{ .size = size };
    VmaAllocationInfo info{};
    VK_CHECK(vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &buffer.buffer, &buffer.allocation, &info));
    buffer.mapped = info.pMappedData;

    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        const VkBufferDeviceAddressInfo addressInfo
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = buffer.buffer
        };
        buffer.address = vkGetBufferDeviceAddress(m_device, &addressInfo);
    }

    setName(VK_OBJECT_TYPE_BUFFER, buffer.buffer, name);
    return buffer;
}

void Context::destroyBuffer(Buffer &buffer) const
{
    if (buffer.buffer) {
        vmaDestroyBuffer(m_allocator, buffer.buffer, buffer.allocation);
    }
    buffer = Buffer{};
}

void Context::write(const Buffer &dst, const void *data, VkDeviceSize size, VkDeviceSize dstOffset) const
{
    VK_CHECK(vmaCopyMemoryToAllocation(m_allocator, data, dst.allocation, dstOffset, size));
}

void Context::flush(const Buffer &buffer) const
{
    VK_CHECK(vmaFlushAllocation(m_allocator, buffer.allocation, 0, VK_WHOLE_SIZE));
}

void Context::upload(const Buffer &dst, const void *data, VkDeviceSize size, VkDeviceSize dstOffset)
{
    Buffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "staging");
    write(staging, data, size);

    immediateSubmit([&](VkCommandBuffer cmd) {
        const VkBufferCopy region{ .srcOffset = 0, .dstOffset = dstOffset, .size = size };
        vkCmdCopyBuffer(cmd, staging.buffer, dst.buffer, 1, &region);

        // Make the copy visible to whatever reads the buffer in later submissions.
        const VkMemoryBarrier2 barrier
        {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT
        };
        const VkDependencyInfo dependency
        {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &barrier
        };
        vkCmdPipelineBarrier2(cmd, &dependency);
    });

    destroyBuffer(staging);
}

Image Context::createImage(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, const char *name) const
{
    const VkImageCreateInfo imageInfo
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent{ .width = extent.width, .height = extent.height, .depth = 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    const VmaAllocationCreateInfo allocInfo{ .usage = VMA_MEMORY_USAGE_AUTO };

    Image image{ .format = format, .extent = extent };
    VK_CHECK(vmaCreateImage(m_allocator, &imageInfo, &allocInfo, &image.image, &image.allocation, nullptr));

    const VkImageViewCreateInfo viewInfo
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange
        {
            .aspectMask = isDepthFormat(format) ? VkImageAspectFlags{VK_IMAGE_ASPECT_DEPTH_BIT}
                                                : VkImageAspectFlags{VK_IMAGE_ASPECT_COLOR_BIT},
            .levelCount = 1,
            .layerCount = 1
        }
    };
    VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &image.view));

    setName(VK_OBJECT_TYPE_IMAGE, image.image, name);
    setName(VK_OBJECT_TYPE_IMAGE_VIEW, image.view, name);
    return image;
}

void Context::destroyImage(Image &image) const
{
    if (image.view) {
        vkDestroyImageView(m_device, image.view, nullptr);
    }
    if (image.image) {
        vmaDestroyImage(m_allocator, image.image, image.allocation);
    }
    image = Image{};
}

void Context::createImage2D(VkCommandBuffer cmd, const unsigned char *pixels, uint32_t width, uint32_t height,
                            Image &outImage, Buffer &outStaging, const char *name) const
{
    outImage = createImage({ width, height }, VK_FORMAT_R8G8B8A8_SRGB,
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, name);

    const VkDeviceSize byteSize = VkDeviceSize{width} * height * 4;
    outStaging = createBuffer(byteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "texture staging");
    write(outStaging, pixels, byteSize);

    transition(cmd, {
        .image = outImage.image,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .dstStage = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT
    });

    const VkBufferImageCopy region
    {
        .imageSubresource{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1 },
        .imageExtent{ .width = width, .height = height, .depth = 1 }
    };
    vkCmdCopyBufferToImage(cmd, outStaging.buffer, outImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    transition(cmd, {
        .image = outImage.image,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcStage = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    });
}

void Context::immediateSubmit(const std::function<void(VkCommandBuffer)> &record)
{
    const VkCommandBufferAllocateInfo allocInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_immediatePool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &cmd));

    const VkCommandBufferBeginInfo beginInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
    {
        const DebugLabel label(cmd, "Immediate submit");
        record(cmd);
    }
    VK_CHECK(vkEndCommandBuffer(cmd));

    const VkCommandBufferSubmitInfo cmdInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = cmd
    };
    const VkSubmitInfo2 submitInfo
    {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdInfo
    };
    m_queue.submit(submitInfo);

    vkFreeCommandBuffers(m_device, m_immediatePool, 1, &cmd);
}

} // namespace gfx
