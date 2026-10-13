#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/vec3.hpp>

#include "gfx/FrameArena.h"
#include "gfx/Vk.h"

namespace render {
    enum class Bucket : uint8_t {Opaque, Mask, Blend};

    enum DrawStateBits : uint8_t
    {
        DrawMirrored    = 1,
        DrawDoubleSided = 2,
    };

    struct DrawRecord
    {
        VkDrawIndexedIndirectCommand command{};
        uint32_t material = 0;
        Bucket   bucket   = Bucket::Opaque;
        uint8_t  state    = 0;
    };

    struct DrawRun
    {
        uint32_t first  = 0;
        uint32_t count  = 0;
        Bucket   bucket = Bucket::Opaque;
        uint8_t  state  = 0;
    };

    enum class DrawPass : uint8_t { Prepass, Lit, Blended };
    constexpr size_t DrawPassCount = 3;

    struct DrawPassList
    {
        gfx::ArenaAllocation commands;
        std::vector<DrawRun> runs;
        uint32_t             count = 0;
    };

    struct SortView
    {
        glm::vec3 position{ 0.0f };
        glm::vec3 forward{ 0.0f, 0.0f, -1.0f };
    };

    class DrawSorter
    {
    public:
       void build(std::span<const DrawRecord> records, std::span<const glm::vec3> centers, const SortView &view,
                   gfx::FrameArena &arena);
        const DrawPassList &list(DrawPass pass) const { return m_lists[static_cast<size_t>(pass)]; }

    private:
        void buildPass(DrawPass pass, std::span<const DrawRecord> records, gfx::FrameArena &arena);

        std::array<DrawPassList, DrawPassCount> m_lists;
        std::vector<uint64_t>                   m_keys;
        std::vector<uint32_t>                   m_depthBits;   // per record, rebuilt each frame
    };
}
