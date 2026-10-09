#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "RenderGraphTypes.h"

namespace render::rg
{

class PassContext;

struct UseDecl
{
    Kind     kind     = Kind::Texture;
    uint32_t resource = UINT32_MAX;
    Usage    usage    = Usage::SampledFragment;
};

struct AttachmentDecl
{
    TextureHandle texture;
    LoadOp        load = LoadOp::DontCare;
    VkClearValue  clear{};
};

struct PassDecl
{
    std::string                        name;
    std::vector<UseDecl>               uses;
    std::vector<AttachmentDecl>        colors;
    std::optional<AttachmentDecl>      depth;
    std::function<void(PassContext &)> body;
};

struct ResourceDecl
{
    Kind        kind = Kind::Texture;
    std::string name;
    bool        imported = false;

    TextureDesc desc;
    VkImage     image  = VK_NULL_HANDLE;
    VkImageView view   = VK_NULL_HANDLE;
    VkFormat    format = VK_FORMAT_UNDEFINED;
    VkExtent2D  extent{};
    uint32_t    sampledSlot = 0;

    VkBuffer buffer = VK_NULL_HANDLE;

    ResourceState                initial;
    std::optional<ResourceState> final;
};

}
