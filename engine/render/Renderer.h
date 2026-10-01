#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

#include <glm/mat4x4.hpp>

#include "gfx/Context.h"
#include "gfx/Swapchain.h"
#include "scene/Scene.h"

class Camera;
class GeometryStore;
class ResourceStore;

namespace render
{

class Renderer
{
public:
    static constexpr uint32_t FramesInFlight = 2;
    static constexpr VkFormat DepthFormat    = VK_FORMAT_D32_SFLOAT;

    Renderer(gfx::Context &ctx, gfx::Swapchain &swapchain, ResourceStore &resources, GeometryStore &geometry)
        : m_ctx(ctx), m_swapchain(swapchain), m_resources(resources), m_geometry(geometry) {}
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    void init(const std::filesystem::path &shaderDir, uint32_t maxDrawsPerFrame);
    void shutdown();

    void render(Scene &scene, const Camera &camera);

    // Recorded after the scene, into the swapchain image with no depth attachment. Used by the editor.
    using Overlay = std::function<void(VkCommandBuffer)>;
    void setOverlay(Overlay overlay) { m_overlay = std::move(overlay); }

private:
    // Per-draw data the vertex shader pulls through renderItemsAddress.
    struct RenderItem
    {
        glm::mat4 wvp;
        glm::mat4 worldMatrix;
        uint32_t  materialIndex = 0;
    };

    struct Frame
    {
        VkCommandPool   commandPool   = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore     imageAcquired = VK_NULL_HANDLE;
        gfx::Buffer     indirectDraws;
        gfx::Buffer     renderItems;
    };

    void createFrames();
    void createPipeline(const std::filesystem::path &shaderDir);
    void resizeDepthIfNeeded();

    // Fills this frame's indirect + render-item buffers. Returns the draw count written.
    uint32_t writeDrawCommands(Frame &frame, const glm::mat4 &viewProj);
    void recordFrame(Frame &frame, uint32_t imageIndex, uint32_t drawCount);

    gfx::Context   &m_ctx;
    gfx::Swapchain &m_swapchain;
    ResourceStore  &m_resources;
    GeometryStore  &m_geometry;

    std::array<Frame, FramesInFlight> m_frames{};
    VkSemaphore m_frameTimeline = VK_NULL_HANDLE;
    uint64_t    m_frameNumber   = 0;   // frames submitted so far; frame n signals n + 1

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;
    gfx::Image       m_depth;

    uint32_t              m_maxDraws = 0;
    std::vector<DrawItem> m_drawItems;   // reused across frames
    Overlay               m_overlay;
};

} // namespace render
