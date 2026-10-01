#include "GeometryStore.h"

#include <format>

#include "gfx/Context.h"

void GeometryStore::reserve(size_t vertexBudgetBytes, size_t indexBudgetBytes)
{
    m_vertexBudgetBytes = vertexBudgetBytes;
    m_indexBudgetBytes  = indexBudgetBytes;
    m_vertices.resize(vertexBudgetBytes / sizeof(Vertex));
    m_indices.resize(indexBudgetBytes / sizeof(uint32_t));
    m_vertOffset = 0;
    m_idxOffset  = 0;
}

void GeometryStore::shutdown()
{
    m_ctx.retire(m_vertexBuffer);
    m_ctx.retire(m_indexBuffer);
    m_vertexBuffer = {};
    m_indexBuffer = {};
    m_meshes.clear();
    m_vertices.clear();
    m_indices.clear();
    m_vertOffset = 0;
    m_idxOffset  = 0;
    m_uploaded = false;
}

void GeometryStore::reset()
{
    shutdown();
    reserve(m_vertexBudgetBytes, m_indexBudgetBytes);
}

size_t GeometryStore::appendVertices(size_t count)
{
    if (m_uploaded) {
        core::fatal("Cannot append geometry after uploadToGpu()");
    }
    if (m_vertOffset + count > m_vertices.size()) {
        core::fatal(std::format("Vertex budget exceeded ({} vertices); raise VertexBudgetBytes", m_vertices.size()));
    }

    const size_t start = m_vertOffset;
    m_vertOffset += count;
    return start;
}

size_t GeometryStore::appendIndices(size_t count)
{
    if (m_uploaded) {
        core::fatal("Cannot append geometry after uploadToGpu()");
    }
    if (m_idxOffset + count > m_indices.size()) {
        core::fatal(std::format("Index budget exceeded ({} indices); raise IndexBudgetBytes", m_indices.size()));
    }

    const size_t start = m_idxOffset;
    m_idxOffset += count;
    return start;
}

uint32_t GeometryStore::addMesh(Mesh &&mesh)
{
    m_meshes.push_back(std::move(mesh));
    return static_cast<uint32_t>(m_meshes.size());
}

bool GeometryStore::uploadToGpu()
{
    if (m_uploaded) {
        core::warn("GeometryStore::uploadToGpu called twice");
        return false;
    }

    const size_t vertexBytes = m_vertOffset * sizeof(Vertex);
    const size_t indexBytes  = m_idxOffset  * sizeof(uint32_t);

    if (!vertexBytes || !indexBytes) {
        core::warn("No geometry to upload");
        return false;
    }

    core::log(std::format("Uploading geometry: {} verts ({} MB), {} indices ({} MB)",
                          m_vertOffset, vertexBytes / 1024 / 1024, m_idxOffset, indexBytes / 1024 / 1024));

    // The vertex shader pulls vertices by address; the index buffer is bound normally.
    m_vertexBuffer = m_ctx.createBuffer(vertexBytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, gfx::MemoryIntent::GpuOnly, "vertices");
    m_indexBuffer = m_ctx.createBuffer(indexBytes,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, gfx::MemoryIntent::GpuOnly, "indices");

    m_ctx.upload(m_vertexBuffer, m_vertices.data(), vertexBytes);
    m_ctx.upload(m_indexBuffer, m_indices.data(), indexBytes);

    m_vertices.clear();
    m_vertices.shrink_to_fit();
    m_indices.clear();
    m_indices.shrink_to_fit();

    m_uploaded = true;
    return true;
}
