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
#include "ResourceStore.h"
#include "SceneExtract.h"
#include "DrawSort.h"
#include "graph/RenderGraph.h"


namespace gfx {
    class StagingUploader;
}

namespace scene { class Scene; }

class Camera;
class GeometryStore;
struct PushConstants;

namespace render
{

struct FrameTargets
{
    rg::TextureHandle swapchain;
    rg::TextureHandle sceneColor;
    rg::TextureHandle depth;
};

class Renderer
{
public:
    static constexpr uint32_t FramesInFlight = 2;
    static constexpr VkFormat DepthFormat      = VK_FORMAT_D32_SFLOAT;
    static constexpr VkFormat SceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    struct PostSettings
    {
        float exposure = 1.0f;
    };
    static constexpr uint32_t MaxGpuScopes   = 16;

    Renderer(gfx::Context &ctx, gfx::Swapchain &swapchain,gfx::StagingUploader &uploader ,ResourceStore &resources, GeometryStore &geometry)
        : m_ctx(ctx), m_uploader(uploader), m_swapchain(swapchain), m_resources(resources), m_geometry(geometry) {}
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    void init(const std::filesystem::path &shaderDir, const std::filesystem::path &cacheDir);
    void shutdown();

    void render(const scene::Scene &scene, const Camera &camera);

    using GraphHook = std::function<void(rg::RenderGraph &, const FrameTargets &)>;
    void setGraphHook(GraphHook hook) { m_graphHook = std::move(hook); }

    const gfx::GpuProfiler &gpuProfiler() const { return m_gpuProfiler; }
    const SceneExtract     &extract() const     { return m_extract; }
    PostSettings           &postSettings()      { return m_post; }
    const rg::RenderGraph  &graph() const       { return m_graph; }
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

    void createFrames();
    void createPipeline(const std::filesystem::path &shaderDir);

    void       declareFrame(uint32_t imageIndex, const PushConstants &pc);
    PipelineId pipelineFor(DrawPass pass, Bucket bucket) const;
    void       recordDraws(const rg::PassContext &ctx, DrawPass pass, const PushConstants &pc) const;
    void recordFrame(Frame &frame);

    gfx::Context   &m_ctx;
    gfx::StagingUploader &m_uploader;

    gfx::Swapchain &m_swapchain;
    ResourceStore  &m_resources;
    GeometryStore  &m_geometry;

    std::array<Frame, FramesInFlight> m_frames{};
    uint64_t    m_frameNumber   = 0;   // frames submitted so far; frame n signals n + 1

    gfx::ShaderCompiler m_shaderCompiler;
    PipelineRegistry    m_pipelines;

    PipelineId          m_prepassPipeline;
    PipelineId          m_prepassMaskPipeline;
    PipelineId          m_litPipeline;
    PipelineId          m_blendPipeline;
    PipelineId          m_postPipeline;

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    uint32_t         m_postSamplerSlot = 0;
    PostSettings     m_post;
    gfx::GpuProfiler m_gpuProfiler;
    SceneExtract     m_extract{ m_ctx, m_geometry, m_resources.materials() };
    DrawSorter       m_draws;
    rg::RenderGraph  m_graph;
    GraphHook        m_graphHook;

    std::chrono::steady_clock::time_point m_startTime = std::chrono::steady_clock::now();

};

} // namespace render
