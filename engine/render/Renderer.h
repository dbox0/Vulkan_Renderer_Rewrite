#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

#include <glm/mat4x4.hpp>

#include "gfx/Context.h"
#include "gfx/Swapchain.h"
#include "gfx/FrameArena.h"
#include "gfx/GpuProfiler.h"
#include "gfx/ShaderCompiler.h"
#include "PipelineRegistry.h"
#include "scene/Scene.h"


namespace gfx {
    class StagingUploader;
}

class Camera;
class GeometryStore;
class ResourceStore;
struct PushConstants;

namespace render
{

class Renderer
{
public:
    static constexpr uint32_t FramesInFlight = 2;
    static constexpr VkFormat DepthFormat    = VK_FORMAT_D32_SFLOAT;
    static constexpr uint32_t MaxGpuScopes   = 8;

    Renderer(gfx::Context &ctx, gfx::Swapchain &swapchain,gfx::StagingUploader &uploader ,ResourceStore &resources, GeometryStore &geometry)
        : m_ctx(ctx), m_uploader(uploader), m_swapchain(swapchain), m_resources(resources), m_geometry(geometry) {}
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    void init(const std::filesystem::path &shaderDir, const std::filesystem::path &cacheDir);
    void shutdown();

    void render(const scene::Scene &scene, const Camera &camera);

    // Recorded after the scene, into the swapchain image with no depth attachment. Used by the editor.
    using Overlay = std::function<void(VkCommandBuffer)>;
    void setOverlay(Overlay overlay) { m_overlay = std::move(overlay); }

    const gfx::GpuProfiler &gpuProfiler() const { return m_gpuProfiler; }
    uint64_t                frameNumber() const { return m_frameNumber; }

    void reloadShaders() { m_pipelines.reloadAll(); }

private:
    struct Frame
    {
        VkCommandPool   commandPool   = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore     imageAcquired = VK_NULL_HANDLE;
        gfx::FrameArena arena;
        uint64_t submitValue = 0;
    };

    struct DrawList
    {
        uint32_t             count = 0;
        uint32_t             firstMirrored = 0;
        gfx::ArenaAllocation commands;
        gfx::ArenaAllocation instances;
    };

    void createFrames();
    void createPipeline(const std::filesystem::path &shaderDir);
    void resizeDepthIfNeeded();

    // Fills this frame's indirect + render-item buffers. Returns the draw count written.
    DrawList writeDrawCommands(Frame &frame);
    void recordFrame(Frame &frame, uint32_t imageIndex, DrawList draws, PushConstants pc);

    gfx::Context   &m_ctx;
    gfx::StagingUploader &m_uploader;

    gfx::Swapchain &m_swapchain;
    ResourceStore  &m_resources;
    GeometryStore  &m_geometry;

    std::array<Frame, FramesInFlight> m_frames{};
    uint64_t    m_frameNumber   = 0;   // frames submitted so far; frame n signals n + 1

    gfx::ShaderCompiler m_shaderCompiler;
    PipelineRegistry    m_pipelines;
    PipelineId          m_scenePipeline;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    gfx::Image       m_depth;
    gfx::GpuProfiler m_gpuProfiler;

    uint32_t              m_maxDraws = 0;
    std::vector<scene::DrawItem> m_drawItems;   // reused across frames
    Overlay               m_overlay;

    std::chrono::steady_clock::time_point m_startTime = std::chrono::steady_clock::now();

};

} // namespace render
