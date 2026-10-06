#include "GeometryStore.h"

#include <algorithm>
#include <format>

#include "gfx/Context.h"
#include "gfx/StagingUploader.h"

namespace
{

constexpr uint32_t White = 0xFFFFFFFFu;

}

void GeometryStore::reserve(size_t maxVertices, size_t maxIndices)
{
    m_maxVertices = maxVertices;
    m_maxIndices  = maxIndices;
    m_positions.resize(maxVertices);
    m_attributes.resize(maxVertices);
    m_colors.resize(maxVertices);
    m_indices.resize(maxIndices);
    m_vertOffset = 0;
    m_idxOffset  = 0;
}

void GeometryStore::shutdown()
{
    m_ctx.retire(m_positionBuffer);
    m_ctx.retire(m_attributeBuffer);
    m_ctx.retire(m_colorBuffer);
    m_ctx.retire(m_indexBuffer);
    m_positionBuffer  = {};
    m_attributeBuffer = {};
    m_colorBuffer     = {};
    m_indexBuffer     = {};
    m_meshes.clear();
    m_positions.clear();
    m_attributes.clear();
    m_colors.clear();
    m_indices.clear();
    m_vertOffset = 0;
    m_idxOffset  = 0;
    m_uploaded = false;
}

void GeometryStore::reset()
{
    shutdown();
    reserve(m_maxVertices, m_maxIndices);
}

size_t GeometryStore::appendVertices(size_t count)
{
    if (m_uploaded) {
        core::fatal("Cannot append geometry after uploadToGpu()");
    }
    if (m_vertOffset + count > m_maxVertices) {
        core::fatal(std::format("Vertex capacity exceeded ({} vertices); raise MaxVertices", m_maxVertices));
    }

    const size_t start = m_vertOffset;
    m_vertOffset += count;

    const auto first = static_cast<std::ptrdiff_t>(start);
    std::fill_n(m_positions.begin() + first, count, glm::vec3(0.0f));
    std::fill_n(m_attributes.begin() + first, count, VertexAttributes{ .normal = 0, .uv = glm::vec2(0.0f) });
    std::fill_n(m_colors.begin() + first, count, White);
    return start;
}

size_t GeometryStore::appendIndices(size_t count)
{
    if (m_uploaded) {
        core::fatal("Cannot append geometry after uploadToGpu()");
    }
    if (m_idxOffset + count > m_maxIndices) {
        core::fatal(std::format("Index capacity exceeded ({} indices); raise MaxIndices", m_maxIndices));
    }

    const size_t start = m_idxOffset;
    m_idxOffset += count;
    return start;
}

GeometryStore::VertexStreams GeometryStore::streamsAt(size_t index)
{
    return VertexStreams
    {
        .positions  = &m_positions[index],
        .attributes = &m_attributes[index],
        .colors     = &m_colors[index]
    };
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

    const size_t positionBytes  = m_vertOffset * sizeof(glm::vec3);
    const size_t attributeBytes = m_vertOffset * sizeof(VertexAttributes);
    const size_t colorBytes     = m_vertOffset * sizeof(uint32_t);
    const size_t indexBytes     = m_idxOffset  * sizeof(uint32_t);

    if (!positionBytes || !indexBytes) {
        core::warn("No geometry to upload");
        return false;
    }

    core::log(std::format("Uploading geometry: {} verts ({} MB), {} indices ({} MB)",
                          m_vertOffset, (positionBytes + attributeBytes + colorBytes) / 1024 / 1024,
                          m_idxOffset, indexBytes / 1024 / 1024));

    constexpr VkBufferUsageFlags streamUsage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    m_positionBuffer  = m_ctx.createBuffer(positionBytes, streamUsage, gfx::MemoryIntent::GpuOnly, "positions");
    m_attributeBuffer = m_ctx.createBuffer(attributeBytes, streamUsage, gfx::MemoryIntent::GpuOnly, "vertex attributes");
    m_colorBuffer     = m_ctx.createBuffer(colorBytes, streamUsage, gfx::MemoryIntent::GpuOnly, "vertex colors");
    m_indexBuffer     = m_ctx.createBuffer(indexBytes,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, gfx::MemoryIntent::GpuOnly, "indices");

    m_uploader.uploadBuffer(m_positionBuffer, 0, m_positions.data(), positionBytes);
    m_uploader.uploadBuffer(m_attributeBuffer, 0, m_attributes.data(), attributeBytes);
    m_uploader.uploadBuffer(m_colorBuffer, 0, m_colors.data(), colorBytes);
    m_uploader.uploadBuffer(m_indexBuffer, 0, m_indices.data(), indexBytes);

    m_positions.clear();
    m_positions.shrink_to_fit();
    m_attributes.clear();
    m_attributes.shrink_to_fit();
    m_colors.clear();
    m_colors.shrink_to_fit();
    m_indices.clear();
    m_indices.shrink_to_fit();

    m_uploaded = true;
    return true;
}
