#include "Renderer.h"

#include <cstring>
#include <format>

#include "GeometryStore.h"
#include "ResourceStore.h"
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
static_assert(sizeof(PostConstants) <= PushConstantSize);

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

    const VkSamplerCreateInfo postSampler
    {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 0.0f
    };
    m_postSamplerSlot = m_resources.samplerSlot(m_resources.addSampler(postSampler));
    m_graph.init(m_ctx, m_resources, FramesInFlight);
}

void Renderer::shutdown()
{
    const VkDevice device = m_ctx.device();

    m_extract.shutdown();
    m_graph.shutdown();
    m_gpuProfiler.destroy(m_ctx);
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
        .colorFormats = { SceneColorFormat },
        .depthFormat = DepthFormat,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthTest = true,
        .depthWrite = true,
        .dynamicFrontFace = true
    });

    m_postPipeline = m_pipelines.addGraphics("post", shaderDir / "post.vert", shaderDir / "post.frag", {
        .layout = m_pipelineLayout,
        .colorFormats = { gfx::Swapchain::Format },
        .cullMode = VK_CULL_MODE_NONE
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
    frameDataPtr->materials      = m_resources.materials().address();
    frameDataPtr->subMeshes      = m_geometry.subMeshesAddress();
    frameDataPtr->time           =  std::chrono::duration<float>(std::chrono::steady_clock::now() - m_startTime).count();
    frameDataPtr->frameIndex     = static_cast<uint32_t>(m_frameNumber);


    m_extract.write(scene, frame.arena);
    m_resources.materials().stage(frame.arena);
    const DrawList draws = writeDrawCommands(frame);

    const PushConstants pc {frameAlloc.address, m_extract.instancesAddress()};

    declareFrame(imageIndex, draws, pc);

    VK_CHECK(vkResetCommandPool(m_ctx.device(), frame.commandPool, 0));
    recordFrame(frame);

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

void Renderer::declareFrame(uint32_t imageIndex, const DrawList &draws, const PushConstants &pc)
{
    const VkExtent2D extent = m_swapchain.extent();
    m_graph.beginFrame(extent);

    const gfx::Image swapchainImage
    {
        .image = m_swapchain.image(imageIndex),
        .view = m_swapchain.view(imageIndex),
        .format = gfx::Swapchain::Format,
        .extent = extent
    };
    const FrameTargets targets
    {
        .swapchain = m_graph.import("swapchain", swapchainImage,
            { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED },
            { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR }),
        .sceneColor = m_graph.create("sceneColor", { .format = SceneColorFormat }),
        .depth = m_graph.create("depth", { .format = DepthFormat })
    };

    constexpr rg::ResourceState vertexRead{ VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT };
    const MaterialTable   &materials = m_resources.materials();
    const rg::BufferHandle instanceBuffer = m_graph.import("instances", m_extract.instanceBuffer(), vertexRead);
    const rg::BufferHandle materialBuffer = m_graph.import("materials", materials.buffer(), vertexRead);

    if (m_extract.hasCopies() || materials.hasCopies()) {
        rg::PassBuilder upload = m_graph.addPass("Upload");
        if (m_extract.hasCopies()) {
            upload.write(instanceBuffer, rg::Usage::TransferDst);
        }
        if (materials.hasCopies()) {
            upload.write(materialBuffer, rg::Usage::TransferDst);
        }
        upload.execute([this](rg::PassContext &ctx) {
            m_extract.recordCopies(ctx.cmd());
            m_resources.materials().recordCopies(ctx.cmd());
        });
    }

    m_graph.addPass("Scene")
        .color(targets.sceneColor, rg::LoadOp::Clear, { .float32 = { 0.3f, 0.3f, 1.0f, 1.0f } })
        .depth(targets.depth, rg::LoadOp::Clear, 0.0f)
        .read(instanceBuffer, rg::Usage::VertexStorageRead)
        .read(materialBuffer, rg::Usage::VertexStorageRead)
        .execute([this, draws, pc](rg::PassContext &ctx) { recordScene(ctx, draws, pc); });

    m_graph.addPass("Post")
        .read(targets.sceneColor, rg::Usage::SampledFragment)
        .color(targets.swapchain, rg::LoadOp::DontCare)
        .execute([this, source = targets.sceneColor](rg::PassContext &ctx) {
            const PostConstants post
            {
                .sourceTexture = ctx.sampledIndex(source),
                .sourceSampler = m_postSamplerSlot,
                .exposure = m_post.exposure
            };
            vkCmdPushConstants(ctx.cmd(), m_pipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof(PostConstants), &post);
            vkCmdBindPipeline(ctx.cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines.get(m_postPipeline));
            vkCmdDraw(ctx.cmd(), 3, 1, 0, 0);
        });

    if (m_graphHook) {
        m_graphHook(m_graph, targets);
    }
    m_graph.compile();
}

void Renderer::recordScene(const rg::PassContext &ctx, const DrawList &draws, const PushConstants &pc) const
{
    if (draws.count == 0) {
        return;
    }
    const VkCommandBuffer cmd = ctx.cmd();
    const VkExtent2D      extent = ctx.renderExtent();
    const VkViewport viewport
    {
        .x = 0.0f,
        .y = static_cast<float>(extent.height),
        .width = static_cast<float>(extent.width),
        .height = -static_cast<float>(extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

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

void Renderer::recordFrame(Frame &frame)
{
    const VkCommandBuffer cmd = frame.commandBuffer;
    const VkCommandBufferBeginInfo beginInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
    m_gpuProfiler.beginFrame(cmd, static_cast<uint32_t>(m_frameNumber % FramesInFlight));

    const VkDescriptorSet globalSet = m_resources.globalDescriptorSet();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &globalSet, 0, nullptr);

    m_graph.execute(cmd, m_gpuProfiler);

    VK_CHECK(vkEndCommandBuffer(cmd));
}

}
