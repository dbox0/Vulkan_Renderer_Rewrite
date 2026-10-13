#include "RenderGraphTypes.h"

namespace render::rg
{

namespace
{

constexpr VkPipelineStageFlags2 DepthStages =
    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

}

ResourceState stateFor(Usage usage)
{
    switch (usage) {
    case Usage::ColorWrite:
        return { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    case Usage::ColorReadWrite:
        return { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    case Usage::DepthWrite:
        return { DepthStages,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                 VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL };
    case Usage::DepthRead:
        return { DepthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL };
    case Usage::SampledFragment:
        return { VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    case Usage::SampledCompute:
        return { VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    case Usage::StorageReadCompute:
        return { VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL };
    case Usage::StorageWriteCompute:
        return { VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL };
    case Usage::VertexStorageRead:
        return { VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED };
    case Usage::StorageReadGraphics:
        return { VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                 VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED };
    case Usage::TransferSrc:
        return { VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL };
    case Usage::TransferDst:
        return { VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL };
    case Usage::IndirectRead:
        return { VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED };
    }
    return {};
}

VkImageUsageFlags imageUsageFor(Usage usage)
{
    switch (usage) {
    case Usage::ColorWrite:
    case Usage::ColorReadWrite:
        return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    case Usage::DepthWrite:
    case Usage::DepthRead:
        return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    case Usage::SampledFragment:
    case Usage::SampledCompute:
        return VK_IMAGE_USAGE_SAMPLED_BIT;
    case Usage::StorageReadCompute:
    case Usage::StorageWriteCompute:
        return VK_IMAGE_USAGE_STORAGE_BIT;
    case Usage::TransferSrc:
        return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    case Usage::TransferDst:
        return VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    case Usage::StorageReadGraphics:
    case Usage::VertexStorageRead:
    case Usage::IndirectRead:
        return 0;
    }

    return 0;
}

bool writes(Usage usage)
{
    switch (usage) {
    case Usage::ColorWrite:
    case Usage::ColorReadWrite:
    case Usage::DepthWrite:
    case Usage::StorageWriteCompute:
    case Usage::TransferDst:
        return true;
    default:
        return false;
    }
}

bool validFor(Usage usage, Kind kind)
{
    switch (usage) {
    case Usage::ColorWrite:
    case Usage::ColorReadWrite:
    case Usage::DepthWrite:
    case Usage::DepthRead:
    case Usage::SampledFragment:
    case Usage::SampledCompute:
        return kind == Kind::Texture;
    case Usage::VertexStorageRead:
    case Usage::IndirectRead:
    case Usage::StorageReadGraphics:
        return kind == Kind::Buffer;
    case Usage::StorageReadCompute:
    case Usage::StorageWriteCompute:
    case Usage::TransferSrc:
    case Usage::TransferDst:
        return true;
    }
    return false;
}

const char *usageName(Usage usage)
{
    switch (usage) {
    case Usage::ColorWrite:          return "ColorWrite";
    case Usage::ColorReadWrite:      return "ColorReadWrite";
    case Usage::DepthWrite:          return "DepthWrite";
    case Usage::DepthRead:           return "DepthRead";
    case Usage::SampledFragment:     return "SampledFragment";
    case Usage::SampledCompute:      return "SampledCompute";
    case Usage::StorageReadCompute:  return "StorageReadCompute";
    case Usage::StorageWriteCompute: return "StorageWriteCompute";
    case Usage::VertexStorageRead:   return "VertexStorageRead";
    case Usage::TransferSrc:         return "TransferSrc";
    case Usage::TransferDst:         return "TransferDst";
    case Usage::IndirectRead:        return "IndirectRead";
    case Usage::StorageReadGraphics: return "StorageReadGraphics";
    }
    return "?";
}

}
