#pragma once
#include <vector>

#include "Vk.h"

namespace gfx
{

class Context;

struct GraphicsPipelineDesc
{
    VkShaderModule        vertex   = VK_NULL_HANDLE;
    VkShaderModule        fragment = VK_NULL_HANDLE;
    VkPipelineLayout      layout   = VK_NULL_HANDLE;
    std::vector<VkFormat> colorFormats;
    VkFormat              depthFormat = VK_FORMAT_UNDEFINED;

    VkPrimitiveTopology topology  = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags     cullMode  = VK_CULL_MODE_NONE;
    VkFrontFace         frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    bool        depthTest    = false;
    bool        depthWrite   = false;
    VkCompareOp depthCompare = VK_COMPARE_OP_GREATER_OR_EQUAL;

    bool dynamicFrontFace = false;
};


VkPipeline tryCreateGraphicsPipeline(const Context &ctx, const GraphicsPipelineDesc &desc, const char *name);
VkPipeline createGraphicsPipeline(const Context &ctx, const GraphicsPipelineDesc &desc, const char *name);

}