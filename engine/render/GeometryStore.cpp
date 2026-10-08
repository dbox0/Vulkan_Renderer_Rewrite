#include "GeometryStore.h"

#include <format>
#include <optional>
#include <string_view>

#include "gfx/Context.h"
#include "gfx/StagingUploader.h"

namespace
{

constexpr VkBufferUsageFlags StreamUsage =
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

constexpr uint64_t MiB = 1024 * 1024;

void warnFull(std::string_view meshName, std::string_view what, const core::RangeAllocator &allocator, uint64_t requested)
{
    core::warn(std::format("Mesh '{}' skipped: needs {} {}, {} of {} used, largest free run {}",
                           meshName, requested, what, allocator.used(), allocator.capacity(), allocator.largestFree()));
}

}

void GeometryStore::init()
{
    const VkDeviceSize positionBytes  = VkDeviceSize{ MaxVertices } * sizeof(glm::vec3);
    const VkDeviceSize attributeBytes = VkDeviceSize{ MaxVertices } * sizeof(VertexAttributes);
    const VkDeviceSize colorBytes     = VkDeviceSize{ MaxVertices } * sizeof(uint32_t);
    const VkDeviceSize indexBytes     = VkDeviceSize{ MaxIndices }  * sizeof(uint32_t);

    m_positionBuffer  = m_ctx.createBuffer(positionBytes, StreamUsage, gfx::MemoryIntent::GpuOnly, "positions");
    m_attributeBuffer = m_ctx.createBuffer(attributeBytes, StreamUsage, gfx::MemoryIntent::GpuOnly, "vertex attributes");
    m_colorBuffer     = m_ctx.createBuffer(colorBytes, StreamUsage, gfx::MemoryIntent::GpuOnly, "vertex colors");
    m_indexBuffer     = m_ctx.createBuffer(indexBytes,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, gfx::MemoryIntent::GpuOnly, "indices");

    core::log(std::format("Geometry buffers: {} vertices, {} indices, {} MB",
                          MaxVertices, MaxIndices, (positionBytes + attributeBytes + colorBytes + indexBytes) / MiB));
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
}

MeshHandle GeometryStore::addMesh(const MeshData &data)
{
    const uint64_t vertexCount = data.positions.size();
    const uint64_t indexCount  = data.indices.size();

    if (data.attributes.size() != vertexCount || data.colors.size() != vertexCount) {
        core::fatal(std::format("Mesh '{}': vertex streams differ in length ({} positions, {} attributes, {} colors)",
                                data.name, vertexCount, data.attributes.size(), data.colors.size()));
    }
    if (vertexCount == 0 || indexCount == 0) {
        core::warn(std::format("Mesh '{}' skipped: {} vertices, {} indices", data.name, vertexCount, indexCount));
        return {};
    }

    const std::optional<uint64_t> vertexOffset = m_vertexAlloc.allocate(vertexCount);
    if (!vertexOffset) {
        warnFull(data.name, "vertices", m_vertexAlloc, vertexCount);
        return {};
    }
    const std::optional<uint64_t> indexOffset = m_indexAlloc.allocate(indexCount);
    if (!indexOffset) {
        m_vertexAlloc.free(*vertexOffset, vertexCount);
        warnFull(data.name, "indices", m_indexAlloc, indexCount);
        return {};
    }

    m_uploader.uploadBuffer(m_positionBuffer, *vertexOffset * sizeof(glm::vec3),
                            data.positions.data(), vertexCount * sizeof(glm::vec3));
    m_uploader.uploadBuffer(m_attributeBuffer, *vertexOffset * sizeof(VertexAttributes),
                            data.attributes.data(), vertexCount * sizeof(VertexAttributes));
    m_uploader.uploadBuffer(m_colorBuffer, *vertexOffset * sizeof(uint32_t),
                            data.colors.data(), vertexCount * sizeof(uint32_t));
    m_uploader.uploadBuffer(m_indexBuffer, *indexOffset * sizeof(uint32_t),
                            data.indices.data(), indexCount * sizeof(uint32_t));

    Mesh mesh
    {
        .name      = data.name,
        .subMeshes = data.subMeshes,
        .vertices  = { .offset = *vertexOffset, .count = vertexCount },
        .indices   = { .offset = *indexOffset, .count = indexCount }
    };
    const auto vertexBase = static_cast<uint32_t>(*vertexOffset);
    const auto indexBase  = static_cast<uint32_t>(*indexOffset);
    for (SubMesh &subMesh : mesh.subMeshes) {
        subMesh.vertexStart += vertexBase;
        subMesh.indexStart  += indexBase;
    }

    uint32_t index = 0;
    if (!m_freeSlots.empty()) {
        index = m_freeSlots.back();
        m_freeSlots.pop_back();
    } else {
        index = static_cast<uint32_t>(m_slots.size());
        m_slots.emplace_back();
    }

    Slot &slot = m_slots[index];
    slot.mesh  = std::move(mesh);
    slot.alive = true;
    ++m_liveMeshes;
    return MeshHandle{ .index = index, .generation = slot.generation };
}

void GeometryStore::removeMesh(MeshHandle handle)
{
    if (!get(handle)) {
        core::warn(std::format("GeometryStore::removeMesh: handle {{{}, {}}} is stale", handle.index, handle.generation));
        return;
    }

    Slot &slot = m_slots[handle.index];
    slot.alive = false;
    ++slot.generation;
    --m_liveMeshes;

    const uint32_t    index    = handle.index;
    const core::Range vertices = slot.mesh.vertices;
    const core::Range indices  = slot.mesh.indices;
    m_ctx.retire([this, index, vertices, indices] {
        m_vertexAlloc.free(vertices.offset, vertices.count);
        m_indexAlloc.free(indices.offset, indices.count);
        m_freeSlots.push_back(index);
    });
}

void GeometryStore::clear()
{
    for (uint32_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].alive) {
            removeMesh(MeshHandle{ .index = i, .generation = m_slots[i].generation });
        }
    }
}

const Mesh *GeometryStore::get(MeshHandle handle) const
{
    if (handle.index >= m_slots.size()) {
        return nullptr;
    }
    const Slot &slot = m_slots[handle.index];
    if (!slot.alive || slot.generation != handle.generation) {
        return nullptr;
    }
    return &slot.mesh;
}
