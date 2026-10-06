#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Types.h"
#include "core/RangeAllocator.h"
#include "gfx/Resources.h"

namespace gfx { class Context; class StagingUploader; }

class GeometryStore
{
public:
    static constexpr uint32_t MaxVertices = 8u * 1024 * 1024;
    static constexpr uint32_t MaxIndices  = 32u * 1024 * 1024;

    GeometryStore(gfx::Context &ctx, gfx::StagingUploader &uploader) : m_ctx(ctx), m_uploader(uploader) {}
    GeometryStore(const GeometryStore &) = delete;
    GeometryStore &operator=(const GeometryStore &) = delete;

    void init();
    void shutdown();

    uint32_t addMesh(const MeshData &data);
    void     removeMesh(uint32_t meshId);
    void     clear();

    const Mesh &mesh(uint32_t meshId) const;
    size_t      liveMeshCount() const { return m_liveMeshes; }

    const core::RangeAllocator &vertexAllocator() const { return m_vertexAlloc; }
    const core::RangeAllocator &indexAllocator()  const { return m_indexAlloc; }

    uint64_t positionsAddress()  const { return m_positionBuffer.address; }
    uint64_t attributesAddress() const { return m_attributeBuffer.address; }
    uint64_t colorsAddress()     const { return m_colorBuffer.address; }
    VkBuffer indexBuffer()       const { return m_indexBuffer.buffer; }

private:
    struct StoredMesh
    {
        Mesh mesh;
        bool alive = false;
    };

    gfx::Context         &m_ctx;
    gfx::StagingUploader &m_uploader;

    core::RangeAllocator    m_vertexAlloc{ MaxVertices };
    core::RangeAllocator    m_indexAlloc{ MaxIndices };
    std::vector<StoredMesh> m_meshes;
    size_t                  m_liveMeshes = 0;

    gfx::Buffer m_positionBuffer;
    gfx::Buffer m_attributeBuffer;
    gfx::Buffer m_colorBuffer;
    gfx::Buffer m_indexBuffer;
};