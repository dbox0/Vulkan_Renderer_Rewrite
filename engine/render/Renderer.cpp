#include "Renderer.h"

#include <cstring>
#include <format>

#include "GeometryStore.h"
#include "ResourceStore.h"
#include "gfx/Barriers.h"
#include "gfx/Pipeline.h"
#include "scene/Camera.h"
#include "gfx/FrameArena.h"
#include "gfx/StagingUploader.h"
#include "scene/Scene.h"

namespace render
{

namespace
{

constexpr uint32_t PushConstantSize = 128;
static_assert(sizeof(PushConstants) <= PushConstantSize);

}

void Renderer::init(const std::filesystem::path &shaderDir, const std::filesystem::path &cacheDir)
{
    m_shaderCompiler.init({ shaderDir }, cacheDir);
    m_pipelines.init(m_ctx, m_shaderCompiler);
    createFrames();
    m_extract.init();
    m_gpuProfiler.init(m_ctx, FramesInFlight, MaxGpuScopes);

    const auto pipelineStart = std::chrono::steady_clock::now();
    createPipeline(shaderDir);
    const std::chrono::duration<double, std::milli> pipelineTime = std::chrono::steady_clock::now() - pipelineStart;
    core::log(std::format("Pipelines: {:.1f} ms ({} SPIR-V hits, {} misses)", pipelineTime.count(),
                          m_pipelines.shaderCacheHits(), m_pipelines.shaderCacheMisses()));
    resizeDepthIfNeeded();
}

void Renderer::shutdown()
{
    const VkDevice device = m_ctx.device();

    m_extract.shutdown();
    m_gpuProfiler.destroy(m_ctx);
    m_ctx.destroyImage(m_depth);
    m_pipelines.destroy();
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

    m_scenePipeline = m_pipelines.addGraphics("scene", shaderDir / "shader.vert", shaderDir / "shader.frag", {
        .layout = m_pipelineLayout,
        .colorFormats = { gfx::Swapchain::Format },
        .depthFormat = DepthFormat,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthTest = true,
        .depthWrite = true,
        .dynamicFrontFace = true
    });
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

Renderer::DrawList Renderer::writeDrawCommands(Frame &frame) const
{
    const std::span<const VkDrawIndexedIndirectCommand> commands = m_extract.commands();
    DrawList draws
    {
        .count = static_cast<uint32_t>(commands.size()),
        .firstMirrored = m_extract.firstMirrored()
    };
    if (draws.count == 0) {
        return draws;
    }
    draws.commands = frame.arena.allocate(commands.size_bytes());
    std::memcpy(draws.commands.cpu, commands.data(), commands.size_bytes());
    return draws;
}

void Renderer::render(const scene::Scene &scene, const Camera &camera)
{
    m_extract.collect(scene);

    if (m_swapchain.needsRecreate()) {
        if (!m_swapchain.recreate()) {
            return;
        }
        resizeDepthIfNeeded();
    }

    Frame &frame = m_frames[m_frameNumber % FramesInFlight];
    m_ctx.queue().wait(frame.submitValue);
    m_ctx.collect();
#ifndef NDEBUG
    m_pipelines.pollChanges();
#endif


    uint32_t imageIndex = 0;
    if (!m_swapchain.acquire(frame.imageAcquired, imageIndex)) {
        return;
    }

    frame.arena.reset();

    // Alloc FrameData
    auto frameAlloc = frame.arena.allocate(sizeof(FrameData));
    auto *frameDataPtr = static_cast<FrameData *>(frameAlloc.cpu);


    const VkExtent2D extent = m_swapchain.extent();
    const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);

    frameDataPtr->viewProj       = camera.viewProjection(aspectRatio);
    frameDataPtr->view           = camera.view();
    frameDataPtr->proj           = camera.projection(aspectRatio) ;
    frameDataPtr->cameraPosition = glm::vec4(camera.position,0.0f);
    frameDataPtr->positions      = m_geometry.positionsAddress();
    frameDataPtr->attributes     = m_geometry.attributesAddress();
    frameDataPtr->colors         = m_geometry.colorsAddress();
    frameDataPtr->materials      = m_resources.materialBufferAddress();
    frameDataPtr->subMeshes      = m_geometry.subMeshesAddress();
    frameDataPtr->time           =  std::chrono::duration<float>(std::chrono::steady_clock::now() - m_startTime).count();
    frameDataPtr->frameIndex     = static_cast<uint32_t>(m_frameNumber);


    m_extract.write(scene, frame.arena);
    const DrawList draws = writeDrawCommands(frame);

    const PushConstants pc {frameAlloc.address, m_extract.instancesAddress()};

    VK_CHECK(vkResetCommandPool(m_ctx.device(), frame.commandPool, 0));
    recordFrame(frame, imageIndex, draws, pc);

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

    frame.arena.flush(m_ctx);
    m_uploader.flush();
    frame.submitValue = m_ctx.queue().submit(submitInfo);
    ++m_frameNumber;

    m_swapchain.present(m_ctx.queue(), imageIndex);

}

void Renderer::recordFrame(Frame &frame, uint32_t imageIndex, DrawList draws, PushConstants pc)
{
    const VkCommandBuffer cmd = frame.commandBuffer;
    const VkCommandBufferBeginInfo beginInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
    m_gpuProfiler.beginFrame(cmd, static_cast<uint32_t>(m_frameNumber % FramesInFlight));
    m_extract.recordUploads(cmd);

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
        .clearValue{ .depthStencil{ .depth = 0.0f } }
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
        const gfx::GpuProfiler::Scope scope(m_gpuProfiler, cmd, "Scene");
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


            vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof(PushConstants), &pc);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines.get(m_scenePipeline));
            vkCmdBindIndexBuffer(cmd, m_geometry.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

            constexpr VkDeviceSize stride = sizeof(VkDrawIndexedIndirectCommand);
            vkCmdSetFrontFace(cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE);
            if (draws.firstMirrored > 0) {
                vkCmdDrawIndexedIndirect(cmd, draws.commands.buffer, draws.commands.offset, draws.firstMirrored, stride);
            }
            if (draws.count > draws.firstMirrored) {
                vkCmdSetFrontFace(cmd, VK_FRONT_FACE_CLOCKWISE);
                vkCmdDrawIndexedIndirect(cmd, draws.commands.buffer, draws.commands.offset + draws.firstMirrored * stride,
                                         draws.count - draws.firstMirrored, stride);
            }
        }

        vkCmdEndRendering(cmd);
    }

    if (m_overlay) {
        const gfx::GpuProfiler::Scope scope(m_gpuProfiler, cmd, "Overlay");
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
        .dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
    });

    VK_CHECK(vkEndCommandBuffer(cmd));
}

}
