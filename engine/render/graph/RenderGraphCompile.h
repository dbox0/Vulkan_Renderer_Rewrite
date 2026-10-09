#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "RenderGraphDecl.h"

namespace render::rg
{

struct Tracked
{
    VkPipelineStageFlags2 writeStages  = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2        writeAccess  = VK_ACCESS_2_NONE;
    VkPipelineStageFlags2 syncedStages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2        syncedAccess = VK_ACCESS_2_NONE;
    VkImageLayout         layout       = VK_IMAGE_LAYOUT_UNDEFINED;
};

inline constexpr VkAccessFlags2 WriteMask =
    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT |
    VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

Tracked trackedFrom(const ResourceState &state);
bool    loadsContents(const PassDecl &pass, const UseDecl &use);

struct BarrierRecord
{
    Kind          kind     = Kind::Texture;
    uint32_t      resource = UINT32_MAX;
    ResourceState src;
    ResourceState dst;
};

struct BarrierRange
{
    uint32_t first = 0;
    uint32_t count = 0;
};

struct BarrierPlan
{
    std::vector<BarrierRecord> barriers;
    std::vector<BarrierRange>  passes;
    BarrierRange               final;
    std::vector<Tracked>       states;
    std::vector<std::string>   errors;
};

void compileBarriers(std::span<const ResourceDecl> resources, std::span<const PassDecl> passes,
                     std::span<const Tracked> initial, BarrierPlan &plan);

}
