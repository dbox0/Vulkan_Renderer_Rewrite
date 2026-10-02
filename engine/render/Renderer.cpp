#include "Renderer.h"

#include <format>

#include "GeometryStore.h"
#include "ResourceStore.h"
#include "gfx/Barriers.h"
#include "gfx/DebugLabel.h"
#include "gfx/Pipeline.h"
#include "scene/Camera.h"
#include "gfx/FrameArena.h"
#include "gfx/StagingUploader.h"

namespace render
{

namespace
{

// Must match the push constant block in shader.vert. One 128-byte range is shared by every pipeline.
struct PushConstants
{
    uint64_t vertexBufferAddress   = 0;
    uint64_t materialBufferAddress = 0;
    uint64_t renderItemsAddress    = 0;
};
constexpr uint32_t PushConstantSize = 128;
static_assert(sizeof(PushConstants) <= PushConstantSize);

}

void Renderer::init(const std::filesystem::path &shaderDir)
{
    m_drawItems.reserve(1024);
    createFrames();
    createPipeline(shaderDir);
    resizeDepthIfNeeded();
}

void Renderer::shutdown()
{
    const VkDevice device = m_ctx.device();

    m_ctx.destroyImage(m_depth);
    vkDestroyPipeline(device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);

    for (Frame &frame : m_frames) {
        frame.arena.destroy(m_ctx);
        vkDestroySemaphore(device, frame.imageAcquired, nullptr);
        vkDestroyCommandPool(device, frame.commandPool, nullptr);
        frame = Frame{};
    }
}



void Renderer::createPipeline(const std::filesystem::path &shaderDir)
{
    const VkPushConstantRange pushRange
    {
        .stageFlags = VK_SHADER_STAGE_ALL,
        .offset = 0,
        .size = PushConstantSize
    };
    const VkDescriptorSetLayout globalLayout = m_resources.globalLayout();
    const VkPipelineLayoutCreateInfo layoutInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &globalLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange
    };
    VK_CHECK(vkCreatePipelineLayout(m_ctx.device(), &layoutInfo, nullptr, &m_pipelineLayout));

    const VkShaderModule vertex = gfx::loadShader(m_ctx, shaderDir / "shader.vert");
    const VkShaderModule fragment = gfx::loadShader(m_ctx, shaderDir / "shader.frag");

    m_pipeline = gfx::createGraphicsPipeline(m_ctx, {
        .vertex = vertex,
        .fragment = fragment,
        .layout = m_pipelineLayout,
        .colorFormats = { gfx::Swapchain::Format },
        .depthFormat = DepthFormat,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthTest = true,
        .depthWrite = true,
        .depthCompare = VK_COMPARE_OP_LESS
    }, "scene");

    vkDestroyShaderModule(m_ctx.device(), vertex, nullptr);
    vkDestroyShaderModule(m_ctx.device(), fragment, nullptr);
}

void Renderer::createFrames() {
    const VkDevice device = m_ctx.device();
    for (Frame &frame : m_frames) {
        const VkCommandPoolCreateInfo poolInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = m_ctx.queueFamily()
        };
        VK_CHECK(vkCreateCommandPool(device,&poolInfo,nullptr,&frame.commandPool));

        const VkCommandBufferAllocateInfo allocInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = frame.commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &frame.commandBuffer));

        const VkSemaphoreCreateInfo semaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.imageAcquired));
        m_ctx.setName(VK_OBJECT_TYPE_SEMAPHORE, frame.imageAcquired, "image acquired");

        frame.arena.init(m_ctx, 16 * 1024 * 1024, "frame arena");
    }
}
void Renderer::resizeDepthIfNeeded()
{
    const VkExtent2D extent = m_swapchain.extent();
    if (m_depth.image && m_depth.extent.width == extent.width && m_depth.extent.height == extent.height) {
        return;
    }
    m_ctx.destroyImage(m_depth);
    m_depth = m_ctx.createImage(extent, DepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, "depth");
}

Renderer::DrawList Renderer::writeDrawCommands(Frame &frame, const glm::mat4 &viewProj)
{

   DrawList draws;
    for (const DrawItem &item : m_drawItems) {
        draws.count += static_cast<uint32_t>(m_geometry.mesh(item.meshId).subMeshes.size());
    }
    if (draws.count == 0) {
        return draws;
    }
    draws.commands = frame.arena.allocate(draws.count * sizeof(VkDrawIndexedIndirectCommand));
    draws.items    = frame.arena.allocate(draws.count * sizeof(RenderItem));
    auto *commands = static_cast<VkDrawIndexedIndirectCommand *>(draws.commands.cpu);
    auto *items    = static_cast<RenderItem *>(draws.items.cpu);

    uint32_t i = 0;
    for (const DrawItem &item : m_drawItems) {
        for (const SubMesh &subMesh : m_geometry.mesh(item.meshId).subMeshes) {
            commands[i] = VkDrawIndexedIndirectCommand
            {
                .indexCount = static_cast<uint32_t>(subMesh.indexCount),
                .instanceCount = 1,
                .firstIndex = static_cast<uint32_t>(subMesh.indexStart),
                .vertexOffset = static_cast<int32_t>(subMesh.vertexStart),
                .firstInstance = i
            };
            items[i] = RenderItem
            {
                .wvp = viewProj * item.worldMatrix,
                .worldMatrix = item.worldMatrix,
                .materialIndex = subMesh.materialId ? subMesh.materialId - 1 : 0
            };
            ++i;
        }
    }
    return draws;
}

void Renderer::render(Scene &scene, const Camera &camera)
{
    if (m_swapchain.needsRecreate()) {
        if (!m_swapchain.recreate()) {
            return;   // the window has no area right now
        }
        resizeDepthIfNeeded();
    }

    Frame &frame = m_frames[m_frameNumber % FramesInFlight];
    m_ctx.queue().wait(frame.submitValue);
    m_ctx.collect();
    frame.arena.reset();
    /*
    m_ctx.retire(m_ctx.createBuffer(1 << 20, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, gfx::Context::MemoryIntent::Upload, "retire test"));
    if (m_frameNumber % 60 == 0) {
        VmaTotalStatistics stats{};
        vmaCalculateStatistics(m_ctx.allocator(),&stats);
        core::log(std::format("allocated {} KB", stats.total.statistics.allocationBytes / 1024));
    }*/

    // A failed acquire consumes nothing: the frame number only advances once a frame is submitted.
    uint32_t imageIndex = 0;
    if (!m_swapchain.acquire(frame.imageAcquired, imageIndex)) {
        return;
    }

    const VkExtent2D extent = m_swapchain.extent();
    const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);

    DrawList draws;
    if (m_geometry.uploaded()) {
        scene.collectDrawItems(m_drawItems);
        draws = writeDrawCommands(frame, camera.viewProjection(aspectRatio));
    }
    frame.arena.flush(m_ctx);

    VK_CHECK(vkResetCommandPool(m_ctx.device(), frame.commandPool, 0));
    recordFrame(frame, imageIndex, draws);

    const VkSemaphoreSubmitInfo waitInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame.imageAcquired,
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
    };
    const std::array signalInfos
    {
        VkSemaphoreSubmitInfo
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = m_swapchain.renderFinished(imageIndex),
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
        },
    };
    const VkCommandBufferSubmitInfo cmdInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame.commandBuffer
    };
    const VkSubmitInfo2 submitInfo
    {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &waitInfo,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdInfo,
        .signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size()),
        .pSignalSemaphoreInfos = signalInfos.data()
    };
    m_uploader.flush();
    frame.submitValue = m_ctx.queue().submit(submitInfo);
    ++m_frameNumber;

    m_swapchain.present(m_ctx.queue(), imageIndex);

}

void Renderer::recordFrame(Frame &frame, uint32_t imageIndex, DrawList draws)
{
    const VkCommandBuffer cmd = frame.commandBuffer;
    const VkCommandBufferBeginInfo beginInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    const VkExtent2D extent = m_swapchain.extent();
    const VkImage swapchainImage = m_swapchain.image(imageIndex);

    gfx::transition(cmd, {
        .image = swapchainImage,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
    });

    gfx::transition(cmd, {
        .image = m_depth.image,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .srcStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .srcAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .aspect = VK_IMAGE_ASPECT_DEPTH_BIT
    });

    const VkRenderingAttachmentInfo colorAttachment
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_swapchain.view(imageIndex),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue{ .color{ .float32 = { 0.3f, 0.3f, 1.0f, 1.0f } } }
    };
    const VkRenderingAttachmentInfo depthAttachment
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_depth.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue{ .depthStencil{ .depth = 1.0f } }
    };
    const VkRenderingInfo renderingInfo
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea{ .extent = extent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment
    };
    {
        const gfx::DebugLabel label(cmd, "Scene");
        vkCmdBeginRendering(cmd, &renderingInfo);

        if (draws.count > 0) {
            // Negative height flips Y so +Y points up, matching glTF and glm.
            const VkViewport viewport
            {
                .x = 0.0f,
                .y = static_cast<float>(extent.height),
                .width = static_cast<float>(extent.width),
                .height = -static_cast<float>(extent.height),
                .minDepth = 0.0f,
                .maxDepth = 1.0f
            };
            const VkRect2D scissor{ .extent = extent };
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            const VkDescriptorSet globalSet = m_resources.globalDescriptorSet();
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &globalSet, 0, nullptr);

            const PushConstants push
            {
                .vertexBufferAddress = m_geometry.vertexBufferAddress(),
                .materialBufferAddress = m_resources.materialBufferAddress(),
                .renderItemsAddress = draws.items.address
            };
            vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof(push), &push);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
            vkCmdBindIndexBuffer(cmd, m_geometry.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexedIndirect(cmd, draws.commands.buffer, draws.commands.offset, draws.count,
                         sizeof(VkDrawIndexedIndirectCommand));
        }

        vkCmdEndRendering(cmd);
    }

    if (m_overlay) {
        const gfx::DebugLabel label(cmd, "Overlay");
        gfx::transition(cmd, {
            .image = swapchainImage,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
        });

        const VkRenderingAttachmentInfo overlayAttachment
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = m_swapchain.view(imageIndex),
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE
        };
        const VkRenderingInfo overlayInfo
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea{ .extent = extent },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &overlayAttachment
        };
        vkCmdBeginRendering(cmd, &overlayInfo);
        m_overlay(cmd);
        vkCmdEndRendering(cmd);
    }

    gfx::transition(cmd, {
        .image = swapchainImage,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT   // must overlap the render-finished signal stage
    });

    VK_CHECK(vkEndCommandBuffer(cmd));
}

}
