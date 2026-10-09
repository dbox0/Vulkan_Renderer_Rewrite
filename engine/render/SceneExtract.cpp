#include "SceneExtract.h"

#include <algorithm>

#include <glm/mat3x3.hpp>
#include <glm/mat3x4.hpp>

#include "GeometryStore.h"
#include "gfx/Barriers.h"
#include "gfx/Context.h"
#include "gfx/FrameArena.h"
#include "scene/Scene.h"

namespace render {

namespace
{

void appendCopy(std::vector<VkBufferCopy> &copies, const VkBufferCopy &region)
{
    if (!copies.empty()) {
        VkBufferCopy &last = copies.back();
        if (last.srcOffset + last.size == region.srcOffset && last.dstOffset + last.size == region.dstOffset) {
            last.size += region.size;
            return;
        }
    }
    copies.push_back(region);
}

const Mesh *meshOf(const scene::ComponentTable<scene::MeshRenderer> &renderers, const GeometryStore &geometry,
                   uint32_t node)
{
    const scene::MeshRenderer *renderer = renderers.find(node);
    return renderer ? geometry.get(renderer->mesh) : nullptr;
}

}

void SceneExtract::init()
{
    m_instances = m_ctx.createBuffer(VkDeviceSize{ MaxInstances } * sizeof(Instance),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        gfx::MemoryIntent::GpuOnly, "instances");
}

void SceneExtract::shutdown()
{
    m_ctx.destroyBuffer(m_instances);
}

void SceneExtract::collect(const scene::Scene &scene)
{
    const scene::ComponentTable<scene::MeshRenderer> &renderers = scene.components<scene::MeshRenderer>();
    std::span<const uint32_t> touched = renderers.touched();
    if (scene.epoch() != m_sceneEpoch) {
        m_sceneEpoch = scene.epoch();
        m_slots.reset();
        m_mirrored.clear();
        m_pending.clear();
        m_isPending.clear();
        touched = renderers.nodes();
    }

    m_slots.update(touched, scene.changedNodes(), [&](uint32_t node) {
        const Mesh *mesh = meshOf(renderers, m_geometry, node);
        return mesh ? static_cast<uint32_t>(mesh->subMeshes.size()) : 0u;
    });

    if (m_isPending.size() < scene.indexCount()) {
        m_isPending.resize(scene.indexCount(), 0);
        m_mirrored.resize(scene.indexCount(), 0);
    }
    for (const uint32_t node : m_slots.dirty()) {
        if (!m_isPending[node]) {
            m_isPending[node] = 1;
            m_pending.push_back(node);
        }
    }
    m_rebuildPending = m_rebuildPending || m_slots.structureChanged();
}

void SceneExtract::write(const scene::Scene &scene, gfx::FrameArena &arena)
{
    m_copies.clear();
    m_stats = {};

    const scene::ComponentTable<scene::MeshRenderer> &renderers = scene.components<scene::MeshRenderer>();
    const std::span<const glm::mat4> worlds = scene.worlds();
    std::sort(m_pending.begin(), m_pending.end());

    for (const uint32_t node : m_pending) {
        m_isPending[node] = 0;
        const Mesh *mesh = meshOf(renderers, m_geometry, node);
        const core::Range range = m_slots.range(node);
        if (!mesh || !range.count) {
            continue;
        }
        const auto count = static_cast<uint32_t>(std::min<uint64_t>(range.count, mesh->subMeshes.size()));
        const VkDeviceSize bytes = VkDeviceSize{ count } * sizeof(Instance);

        const gfx::ArenaAllocation allocation = arena.allocate(bytes);
        auto *records = static_cast<Instance *>(allocation.cpu);

        const glm::mat4   &world = worlds[node];
        const glm::mat4    rows  = glm::transpose(world);
        const glm::mat3x4  packed(rows[0], rows[1], rows[2]);
        const auto         firstTableEntry = static_cast<uint32_t>(mesh->table.offset);
        for (uint32_t u = 0; u < count; ++u) {
            records[u] = Instance{ .worldMatrix = packed, .subMesh = firstTableEntry + u, .node = node };
        }

        appendCopy(m_copies, VkBufferCopy{ .srcOffset = allocation.offset,
                                           .dstOffset = range.offset * sizeof(Instance),
                                           .size = bytes });
        m_copySource = allocation.buffer;
        m_stats.written += count;

        const uint8_t mirrored = glm::determinant(glm::mat3(world)) < 0.0f ? 1 : 0;
        if (m_mirrored[node] != mirrored) {
            m_mirrored[node] = mirrored;
            m_rebuildPending = true;
        }
    }
    m_pending.clear();

    if (m_rebuildPending) {
        rebuildCommands(scene);
        m_rebuildPending = false;
    }

    m_stats.copyRegions = static_cast<uint32_t>(m_copies.size());
    m_stats.live = m_slots.liveInstances();
}

void SceneExtract::rebuildCommands(const scene::Scene &scene)
{
    m_commands.clear();
    m_mirroredCommands.clear();

    const scene::ComponentTable<scene::MeshRenderer> &renderers = scene.components<scene::MeshRenderer>();
    const std::span<const uint32_t>                   nodes     = renderers.nodes();
    const std::span<const scene::MeshRenderer>        values    = renderers.values();

    for (uint32_t slot = 0; slot < renderers.size(); ++slot) {
        const uint32_t    node  = nodes[slot];
        const core::Range range = m_slots.range(node);
        const Mesh       *mesh  = m_geometry.get(values[slot].mesh);
        if (!range.count || !mesh) {
            continue;
        }

        std::vector<VkDrawIndexedIndirectCommand> &out = m_mirrored[node] ? m_mirroredCommands : m_commands;
        const auto count = static_cast<uint32_t>(std::min<uint64_t>(range.count, mesh->subMeshes.size()));
        for (uint32_t u = 0; u < count; ++u) {
            const SubMesh &subMesh = mesh->subMeshes[u];
            out.push_back(VkDrawIndexedIndirectCommand
            {
                .indexCount = subMesh.indexCount,
                .instanceCount = 1,
                .firstIndex = subMesh.indexStart,
                .vertexOffset = static_cast<int32_t>(subMesh.vertexStart),
                .firstInstance = static_cast<uint32_t>(range.offset) + u
            });
        }
    }

    m_firstMirrored = static_cast<uint32_t>(m_commands.size());
    m_commands.insert(m_commands.end(), m_mirroredCommands.begin(), m_mirroredCommands.end());
}

void SceneExtract::recordUploads(VkCommandBuffer cmd) const
{
    if (m_copies.empty()) {
        return;
    }
    gfx::bufferBarrier(cmd, m_instances.buffer,
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    vkCmdCopyBuffer(cmd, m_copySource, m_instances.buffer, static_cast<uint32_t>(m_copies.size()), m_copies.data());

    gfx::bufferBarrier(cmd, m_instances.buffer,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

}
