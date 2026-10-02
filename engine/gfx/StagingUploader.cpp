#include "StagingUploader.h"
#include <chrono>
#include <cstring>
#include "gfx/Context.h"
#include <format>
#include <algorithm>
#include "core/Log.h"

namespace gfx {
    namespace {
        constexpr VkDeviceSize UploadAlign = 16;

        uint64_t alignUp(uint64_t x, uint64_t a) {
            return (x + a - 1) & ~(a - 1);
        }

        void flushMapped(Context &ctx, const Buffer &buf, VkDeviceSize offset, VkDeviceSize size) {
            VK_CHECK(vmaFlushAllocation(ctx.allocator(), buf.allocation, offset, size));
        }
    }

    void StagingUploader::init(Context &context,VkDeviceSize size) {

        constexpr uint64_t MinRing = 64ull << 10;
        if (size < MinRing || (size & (size - 1)) != 0) {
            core::fatal(std::format("Staging ring must be a power of two and at least {} bytes. Given: {}", MinRing, size));
        }

        m_ctx = &context;
        m_ringSize = size;
        m_head = m_tail = 0;
        m_lastValue = 0;
        m_stats = {};
        m_batches.clear();
        m_cmds.clear();
        m_endImageBarriers.clear();
        m_oneOff.clear();

        m_ring = context.createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,gfx::MemoryIntent::Upload,"Staging Ring");
        m_mapped = static_cast<uint8_t *>(m_ring.mapped);
        if (!m_mapped) core::fatal("Staging ring is not host mapped");

        const VkCommandPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = context.queueFamily()
        };
        VK_CHECK(vkCreateCommandPool(context.device(), &poolInfo, nullptr, &m_pool));
    }

    void StagingUploader::destroy() {
        if (!m_ctx) return;
        if (m_cmd != VK_NULL_HANDLE) core::fatal("Staging uploader destroyed with an open batch");
        if (m_ctx->queue().completed() < m_lastValue) core::fatal("Staging uploader destroyed with work in flight");

        vkDestroyCommandPool(m_ctx->device(), m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
        m_cmds.clear();
        m_batches.clear();
        m_ctx->retire(m_ring);
        m_ring = {};
        m_mapped = nullptr;
        m_ctx = nullptr;
    }

    StagingUploader::Allocation StagingUploader::allocate(VkDeviceSize size, VkDeviceSize align) {
        if (size > m_ringSize / 2) {
            core::fatal(std::format("Staging allocation of {} bytes exceeds half the ring ({})", size, m_ringSize / 2));
        }
        uint64_t pos = 0;

        for (;;) {
            reclaim();
            pos = placeAt(size,align);
            if (fits(pos,size)) break;
            stallForSpace();
        }

        m_head = pos + size;
        m_stats.bytes += size;
        const VkDeviceSize offset = pos & (m_ringSize - 1);
        return {m_mapped + offset, offset};
    }

    VkCommandBuffer StagingUploader::acquireCmd() {
        if (!m_cmds.empty() && m_cmds.front().value <= m_ctx->queue().completed()) {
            const VkCommandBuffer cmd = m_cmds.front().cmd;
            m_cmds.pop_front();
            VK_CHECK(vkResetCommandBuffer(cmd,0));
            return cmd;
        }
        const VkCommandBufferAllocateInfo info
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = m_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateCommandBuffers(m_ctx->device(), &info, &cmd));
        return cmd;
    }

    void StagingUploader::openBatch() {
        if (m_cmd != VK_NULL_HANDLE) {
            return;
        }
        m_cmd = acquireCmd();
        const VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        VK_CHECK(vkBeginCommandBuffer(m_cmd, &beginInfo));

        if (vkCmdBeginDebugUtilsLabelEXT) {
            const VkDebugUtilsLabelEXT label
            {
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
                .pLabelName = "Upload Batch"
            };
            vkCmdBeginDebugUtilsLabelEXT(m_cmd,&label);
        }
        recordStartBarrier();
    }

    void StagingUploader::recordStartBarrier() {
        const VkMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
                            VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT |
                             VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT
        };
        const VkDependencyInfo dep{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &barrier
        };
        vkCmdPipelineBarrier2(m_cmd, &dep);
    }

    void StagingUploader::recordEndBarriers() {
        const VkMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
                            VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT |
                             VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
        };
        const VkDependencyInfo dep{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &barrier,
            .imageMemoryBarrierCount = static_cast<uint32_t>(m_endImageBarriers.size()),
            .pImageMemoryBarriers = m_endImageBarriers.data()
        };
        vkCmdPipelineBarrier2(m_cmd, &dep);
    }

    void StagingUploader::uploadBuffer(const Buffer &dst,VkDeviceSize dstOffset, const void* data, VkDeviceSize size) {
        const auto *src = static_cast<const uint8_t *>(data);
        const VkDeviceSize maxChunk = m_ringSize/2;

        VkDeviceSize done = 0;
        while (done < size) {
            const VkDeviceSize chunk = std::min(size - done, maxChunk);
            const Allocation a = allocate(chunk, UploadAlign);
            openBatch();

            std::memcpy(a.cpu, src + done, chunk);
            flushMapped(*m_ctx, m_ring, a.offset, chunk);

            const VkBufferCopy2 region{
                .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
                .srcOffset = a.offset,
                .dstOffset = dstOffset + done,
                .size = chunk
            };
            const VkCopyBufferInfo2 info{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = m_ring.buffer,
                .dstBuffer = dst.buffer,
                .regionCount = 1,
                .pRegions = &region
            };
            vkCmdCopyBuffer2(m_cmd, &info);
            done += chunk;
        }
    }

    void StagingUploader::uploadImage(const Image &dst, const void *pixels, VkDeviceSize size) {
        VkBuffer srcBuffer = VK_NULL_HANDLE;
        VkDeviceSize srcOffset = 0;

        if (size > m_ringSize / 2) {
            const Buffer big = m_ctx->createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryIntent::Upload, "Staging One-Off");
            std::memcpy(big.mapped, pixels, size);
            flushMapped(*m_ctx, big, 0, VK_WHOLE_SIZE);
            m_oneOff.push_back(big);
            openBatch();
            srcBuffer = big.buffer;
        } else {
            const Allocation a = allocate(size, UploadAlign);
            openBatch();
            std::memcpy(a.cpu, pixels, size);
            flushMapped(*m_ctx, m_ring, a.offset, size);
            srcBuffer = m_ring.buffer;
            srcOffset = a.offset;
        }

        const VkImageSubresourceRange range{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = dst.mipLevels,
            .baseArrayLayer = 0,
            .layerCount = 1
        };

        const VkImageMemoryBarrier2 toTransfer{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = dst.image,
            .subresourceRange = range
        };
        const VkDependencyInfo toTransferDep{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &toTransfer
        };
        vkCmdPipelineBarrier2(m_cmd, &toTransferDep);

        const VkBufferImageCopy2 region{
            .sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
            .bufferOffset = srcOffset,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageExtent = {dst.extent.width, dst.extent.height, 1}
        };
        const VkCopyBufferToImageInfo2 copy{
            .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2,
            .srcBuffer = srcBuffer,
            .dstImage = dst.image,
            .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .regionCount = 1,
            .pRegions = &region
        };
        vkCmdCopyBufferToImage2(m_cmd, &copy);

        m_endImageBarriers.push_back({
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = dst.image,
            .subresourceRange = range
        });
    }


    bool StagingUploader::fits(uint64_t pos, VkDeviceSize size) const {
        return pos + size - m_tail <= m_ringSize;
    }

    void StagingUploader::reclaim() {
        const uint64_t completed = m_ctx->queue().completed();
        while (!m_batches.empty() && m_batches.front().value <= completed) {
            m_tail = m_batches.front().end;
            m_batches.pop_front();
        }
    }


    uint64_t StagingUploader::placeAt(VkDeviceSize size, VkDeviceSize align) const{
        uint64_t pos = alignUp(m_head, align);
        uint64_t phys = pos & (m_ringSize - 1);
        if (phys + size > m_ringSize) {
            pos += m_ringSize - phys;
        }
        return pos;
    }

    void StagingUploader::stallForSpace() {
        if (m_cmd != VK_NULL_HANDLE) {
            flush();
            return;
        }
        if (m_batches.empty()) {
            core::fatal("Staging ring cannot satisfy request and nothing is in flight");
        }

        const Batch oldest = m_batches.front();
        const auto start = std::chrono::steady_clock::now();
        m_ctx->queue().wait(oldest.value);
        const auto elapsed = std::chrono::steady_clock::now() - start;

        m_stats.stalls++;
        m_stats.stallNs += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
        m_tail = oldest.end;
        m_batches.pop_front();
    }

    uint64_t StagingUploader::flush() {
        if (m_cmd == VK_NULL_HANDLE) return m_lastValue;

        recordEndBarriers();
        if (vkCmdEndDebugUtilsLabelEXT) vkCmdEndDebugUtilsLabelEXT(m_cmd);
        VK_CHECK(vkEndCommandBuffer(m_cmd));

        const VkCommandBufferSubmitInfo cmdInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = m_cmd
        };
        const VkSubmitInfo2 submit{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &cmdInfo
        };
        const uint64_t value = m_ctx->queue().submit(submit);

        m_batches.push_back({m_head, value});
        m_cmds.push_back({m_cmd, value});

        if (!m_oneOff.empty()) {

            m_ctx->retireAt(value, [ctx = m_ctx, buffers = std::move(m_oneOff)]() mutable {
                for (Buffer &b : buffers) ctx->retire(b);
            });
            m_oneOff.clear();
        }

        m_cmd = VK_NULL_HANDLE;
        m_endImageBarriers.clear();
        m_lastValue = value;
        return value;
    }

    StagingUploader::Stats StagingUploader::takeStats() {
        const Stats s = m_stats;
        m_stats = {};
        return s;
    }
}
