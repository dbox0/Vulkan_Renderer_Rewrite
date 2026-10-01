#pragma once
#include "Vk.h"

namespace gfx
{

struct ImageTransition
{
    VkImage               image     = VK_NULL_HANDLE;
    VkImageLayout         oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout         newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 srcStage  = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2        srcAccess = VK_ACCESS_2_NONE;
    VkPipelineStageFlags2 dstStage  = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2        dstAccess = VK_ACCESS_2_NONE;
    VkImageAspectFlags    aspect    = VK_IMAGE_ASPECT_COLOR_BIT;
};

inline void transition(VkCommandBuffer cmd, const ImageTransition &t)
{
    const VkImageMemoryBarrier2 barrier
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = t.srcStage,
        .srcAccessMask = t.srcAccess,
        .dstStageMask = t.dstStage,
        .dstAccessMask = t.dstAccess,
        .oldLayout = t.oldLayout,
        .newLayout = t.newLayout,
        .image = t.image,
        .subresourceRange
        {
            .aspectMask = t.aspect,
            .levelCount = VK_REMAINING_MIP_LEVELS,
            .layerCount = VK_REMAINING_ARRAY_LAYERS
        }
    };
    const VkDependencyInfo dependency
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };
    vkCmdPipelineBarrier2(cmd, &dependency);
}

}