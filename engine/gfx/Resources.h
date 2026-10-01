#pragma once
#include "Vk.h"

namespace gfx
{

struct Buffer
{
    VkBuffer        buffer     = VK_NULL_HANDLE;
    VmaAllocation   allocation = nullptr;
    VkDeviceSize    size       = 0;
    VkDeviceAddress address    = 0;         // set when created with SHADER_DEVICE_ADDRESS usage
    void           *mapped     = nullptr;   // set for host-visible buffers
};

struct Image
{
    VkImage       image      = VK_NULL_HANDLE;
    VkImageView   view       = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkFormat      format     = VK_FORMAT_UNDEFINED;
    VkExtent2D    extent{};
};

}
