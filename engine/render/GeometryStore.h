#pragma once
#include <cstdint>
#include <vector>

#include "Types.h"
#include "gfx/Resources.h"


namespace gfx { class Context; class StagingUploader; }

// single global vertex + index buffer
// Vertices and indices have NO reserved slot 0
// vertexStart == 0 is a legit first submesh. Mesh IDs are 1-based

class GeometryStore
{
public:
    explicit GeometryStore(gfx::Context &ctx, gfx::StagingUploader &uploader) : m_ctx(ctx), m_uploader(uploader) {}
    GeometryStore(const GeometryStore &) = delete;
    GeometryStore &operator=(const GeometryStore &) = delete;

    void reserve(size_t vertexBudgetBytes, size_t indexBudgetBytes);
    void shutdown();
    void reset();   // drops all meshes and GPU buffers; the GPU must be idle

    size_t appendVertices(size_t count);
    size_t appendIndices(size_t count);

    Vertex   *vertexAt(size_t index) { return &m_vertices[index]; }
    uint32_t *indexAt(size_t index)  { return &m_indices[index];  }

    uint32_t addMesh(Mesh &&mesh);                          // -> 1-based mesh ID
    const Mesh &mesh(uint32_t meshId) const { return m_meshes[meshId - 1]; }
    size_t meshCount() const { return m_meshes.size(); }

    bool uploadToGpu();
    bool uploaded() const { return m_uploaded; }

    uint64_t vertexBufferAddress() const { return m_vertexBuffer.address; }
    VkBuffer indexBuffer()         const { return m_indexBuffer.buffer; }

private:
    gfx::Context &m_ctx;
    gfx::StagingUploader &m_uploader;

    std::vector<Vertex>   m_vertices;
    std::vector<uint32_t> m_indices;
    size_t m_vertOffset = 0;
    size_t m_idxOffset  = 0;
    size_t m_vertexBudgetBytes = 0;
    size_t m_indexBudgetBytes  = 0;
    std::vector<Mesh> m_meshes;

    gfx::Buffer m_vertexBuffer;
    gfx::Buffer m_indexBuffer;
    bool m_uploaded = false;
};
