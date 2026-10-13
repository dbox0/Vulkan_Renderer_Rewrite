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

    constexpr std::array sceneDynamicStates{ VK_DYNAMIC_STATE_FRONT_FACE, VK_DYNAMIC_STATE_CULL_MODE };
    const std::vector<VkDynamicState> dynamicStates(sceneDynamicStates.begin(), sceneDynamicStates.end());

    m_prepassPipeline = m_pipelines.addGraphics("prepass", shaderDir / "depth.vert", {}, {
        .layout = m_pipelineLayout,
        .depthFormat = DepthFormat,
        .depthTest = true,
        .depthWrite = true,
        .depthCompare = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .dynamicStates = dynamicStates
    });

    m_prepassMaskPipeline = m_pipelines.addGraphics("prepass_mask", shaderDir / "depth_mask.vert",
                                                    shaderDir / "depth_mask.frag", {
        .layout = m_pipelineLayout,
        .depthFormat = DepthFormat,
        .depthTest = true,
        .depthWrite = true,
        .depthCompare = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .dynamicStates = dynamicStates
    });

    m_litPipeline = m_pipelines.addGraphics("lit", shaderDir / "lit.vert", shaderDir / "lit.frag", {
        .layout = m_pipelineLayout,
        .colorFormats = { SceneColorFormat },
        .depthFormat = DepthFormat,
        .depthTest = true,
        .depthWrite = false,
        .depthCompare = VK_COMPARE_OP_EQUAL,
        .dynamicStates = dynamicStates
    });

    m_blendPipeline = m_pipelines.addGraphics("blend", shaderDir / "lit.vert", shaderDir / "lit.frag", {
        .layout = m_pipelineLayout,
        .colorFormats = { SceneColorFormat },
        .depthFormat = DepthFormat,
        .depthTest = true,
        .depthWrite = false,
        .depthCompare = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .alphaBlend = true,
        .dynamicStates = dynamicStates
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
    frameDataPtr->debugView      = m_lighting.debugView;
    writeSun(scene, *frameDataPtr);


    m_extract.write(scene, frame.arena);
    m_resources.materials().stage(frame.arena);
    const glm::mat4 cameraRotation = camera.rotation();
    const SortView sortView{ .position = camera.position, .forward = -glm::vec3(cameraRotation[2]) };
    const auto sortStart = std::chrono::steady_clock::now();
    m_draws.build(m_extract.records(), m_extract.centers(), sortView, frame.arena);
    m_cpuStats.sortMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - sortStart).count();
    m_cpuStats.recordMs = 0.0f;
    m_cpuStats.draws = 0;
    m_cpuStats.runs = 0;

    const PushConstants pc {frameAlloc.address, m_extract.instancesAddress()};

    declareFrame(imageIndex, pc);

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

void Renderer::writeSun(const scene::Scene &scene, FrameData &frameData)
{
    const scene::ComponentTable<scene::Light> &lights = scene.components<scene::Light>();
    const std::span<const glm::mat4> worlds = scene.worlds();

    m_sunFromScene = false;
    glm::vec3 direction(0.0f, 1.0f, 0.0f);
    glm::vec3 radiance = m_lighting.sunColor * m_lighting.sunIntensity;
    for (uint32_t i = 0; i < lights.size(); ++i) {
        const scene::Light &light = lights.values()[i];
        if (light.type == scene::Light::Type::Directional) {
            // KHR_lights_punctual: the light shines down its local -Z, so +Z points toward it.
            direction = glm::normalize(glm::vec3(worlds[lights.nodes()[i]][2]));
            radiance = light.color * light.intensity;
            m_sunFromScene = true;
            break;
        }
    }
    if (!m_sunFromScene) {
        const float cosElevation = std::cos(m_lighting.sunElevation);
        direction = glm::vec3(cosElevation * std::sin(m_lighting.sunAzimuth), std::sin(m_lighting.sunElevation),
                              cosElevation * std::cos(m_lighting.sunAzimuth));
    }
    frameData.sunDirection = glm::vec4(direction, 0.0f);
    frameData.sunRadiance = glm::vec4(radiance, m_lighting.sunEnabled ? 1.0f : 0.0f);
}

void Renderer::declareFrame(uint32_t imageIndex, const PushConstants &pc)
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

    const MaterialTable   &materials = m_resources.materials();
    const rg::BufferHandle instanceBuffer = m_graph.import("instances", m_extract.instanceBuffer(), rg::stateFor(rg::Usage::VertexStorageRead));
    const rg::BufferHandle materialBuffer = m_graph.import("materials", materials.buffer(),rg::stateFor(rg::Usage::StorageReadGraphics));

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

    m_graph.addPass("Prepass")
        .depth(targets.depth, rg::LoadOp::Clear, 0.0f)
        .read(instanceBuffer, rg::Usage::VertexStorageRead)
        .read(materialBuffer, rg::Usage::StorageReadGraphics)
        .execute([this, pc](rg::PassContext &ctx) { recordDraws(ctx, DrawPass::Prepass, pc); });

    m_graph.addPass("Lit")
        .color(targets.sceneColor, rg::LoadOp::Clear, { .float32 = { 0.3f, 0.3f, 1.0f, 1.0f } })
        .depth(targets.depth, rg::LoadOp::Load, 0.0f, false)
        .read(instanceBuffer, rg::Usage::VertexStorageRead)
        .read(materialBuffer, rg::Usage::StorageReadGraphics)
        .execute([this, pc](rg::PassContext &ctx) {
            recordDraws(ctx, DrawPass::Lit, pc);
            recordDraws(ctx, DrawPass::Blended, pc);
        });

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

PipelineId Renderer::pipelineFor(DrawPass pass, Bucket bucket) const
{
    switch (pass) {
    case DrawPass::Prepass: return bucket == Bucket::Mask ? m_prepassMaskPipeline : m_prepassPipeline;
    case DrawPass::Lit:     return m_litPipeline;
    case DrawPass::Blended: return m_blendPipeline;
    }
    return m_litPipeline;
}

void Renderer::recordDraws(const rg::PassContext &ctx, DrawPass pass, const PushConstants &pc) const
{
    const DrawPassList &list = m_draws.list(pass);
    if (list.count == 0) {
        return;
    }
    const auto recordStart = std::chrono::steady_clock::now();
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
    vkCmdBindIndexBuffer(cmd, m_geometry.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

    constexpr VkDeviceSize stride = sizeof(VkDrawIndexedIndirectCommand);
    VkPipeline bound = VK_NULL_HANDLE;
    for (const DrawRun &run : list.runs) {
        const VkPipeline pipeline = m_pipelines.get(pipelineFor(pass, run.bucket));
        if (pipeline != bound) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            bound = pipeline;
        }
        vkCmdSetFrontFace(cmd, (run.state & DrawMirrored) ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE);
        vkCmdSetCullMode(cmd, (run.state & DrawDoubleSided) ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
        vkCmdDrawIndexedIndirect(cmd, list.commands.buffer, list.commands.offset + run.first * stride, run.count, stride);
    }

    m_cpuStats.recordMs += std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - recordStart).count();
    m_cpuStats.draws += list.count;
    m_cpuStats.runs += static_cast<uint32_t>(list.runs.size());
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
