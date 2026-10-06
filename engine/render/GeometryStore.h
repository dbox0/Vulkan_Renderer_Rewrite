#pragma once
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

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

    struct VertexStreams
    {
        glm::vec3        *positions  = nullptr;
        VertexAttributes *attributes = nullptr;
        uint32_t         *colors     = nullptr;
    };

    void reserve(size_t maxVertices, size_t maxIndices);
    void shutdown();
    void reset();   // drops all meshes. GPU buffers are retired

    size_t appendVertices(size_t count);
    size_t appendIndices(size_t count);

    VertexStreams streamsAt(size_t index);
    uint32_t     *indexAt(size_t index) { return &m_indices[index]; }

    uint32_t addMesh(Mesh &&mesh);                          // -> 1-based mesh ID
    const Mesh &mesh(uint32_t meshId) const { return m_meshes[meshId - 1]; }
    size_t meshCount() const { return m_meshes.size(); }

    bool uploadToGpu();
    bool uploaded() const { return m_uploaded; }

    uint64_t positionsAddress()  const { return m_positionBuffer.address; }
    uint64_t attributesAddress() const { return m_attributeBuffer.address; }
    uint64_t colorsAddress()     const { return m_colorBuffer.address; }
    VkBuffer indexBuffer()       const { return m_indexBuffer.buffer; }

private:
    gfx::Context &m_ctx;
    gfx::StagingUploader &m_uploader;

    std::vector<glm::vec3>        m_positions;
    std::vector<VertexAttributes> m_attributes;
    std::vector<uint32_t>         m_colors;
    std::vector<uint32_t>         m_indices;
    size_t m_vertOffset  = 0;
    size_t m_idxOffset   = 0;
    size_t m_maxVertices = 0;
    size_t m_maxIndices  = 0;
    std::vector<Mesh> m_meshes;

    gfx::Buffer m_positionBuffer;
    gfx::Buffer m_attributeBuffer;
    gfx::Buffer m_colorBuffer;
    gfx::Buffer m_indexBuffer;
    bool m_uploaded = false;
};
