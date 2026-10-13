#pragma once
#include <cstdint>

#include "gfx/Vk.h"

namespace render::rg
{

struct TextureHandle
{
    uint32_t index = UINT32_MAX;
    bool valid() const { return index != UINT32_MAX; }
    bool operator==(const TextureHandle &) const = default;
};

struct BufferHandle
{
    uint32_t index = UINT32_MAX;
    bool valid() const { return index != UINT32_MAX; }
    bool operator==(const BufferHandle &) const = default;
};

enum class SizeMode : uint8_t { SwapchainRelative, Absolute };

struct TextureDesc
{
    VkFormat   format   = VK_FORMAT_UNDEFINED;
    SizeMode   sizeMode = SizeMode::SwapchainRelative;
    float      scale    = 1.0f;
    VkExtent2D extent{};
};

struct ResourceState
{
    VkPipelineStageFlags2 stage  = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2        access = VK_ACCESS_2_NONE;
    VkImageLayout         layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

enum class Usage : uint8_t
{
    ColorWrite,
    ColorReadWrite,
    DepthWrite,
    DepthRead,
    SampledFragment,
    SampledCompute,
    StorageReadCompute,
    StorageWriteCompute,
    VertexStorageRead,
    StorageReadGraphics,
    TransferSrc,
    TransferDst,
    IndirectRead,
};

enum class LoadOp : uint8_t { Clear, Load, DontCare };

enum class Kind : uint8_t { Texture, Buffer };

ResourceState     stateFor(Usage usage);
VkImageUsageFlags imageUsageFor(Usage usage);
bool          writes(Usage usage);
bool          validFor(Usage usage, Kind kind);
const char   *usageName(Usage usage);

}
