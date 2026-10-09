#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "RenderGraphCompile.h"
#include "RenderGraphDecl.h"
#include "RenderGraphTypes.h"
#include "gfx/Resources.h"

class ResourceStore;

namespace gfx
{
class Context;
class GpuProfiler;
}

namespace render::rg
{

class RenderGraph;

class PassBuilder
{
public:
    PassBuilder &color(TextureHandle target, LoadOp load, VkClearColorValue clear = {});
    PassBuilder &depth(TextureHandle target, LoadOp load, float clear = 0.0f, bool write = true);
    PassBuilder &read(TextureHandle texture, Usage usage);
    PassBuilder &write(TextureHandle texture, Usage usage);
    PassBuilder &read(BufferHandle buffer, Usage usage);
    PassBuilder &write(BufferHandle buffer, Usage usage);
    void         execute(std::function<void(PassContext &)> body);

private:
    friend class RenderGraph;
    PassBuilder(RenderGraph &graph, uint32_t pass) : m_graph(graph), m_pass(pass) {}

    PassBuilder &use(Kind kind, uint32_t resource, Usage usage, bool expectWrite);
    PassDecl    &pass();

    RenderGraph &m_graph;
    uint32_t     m_pass;
};

class PassContext
{
public:
    VkCommandBuffer cmd() const          { return m_cmd; }
    VkExtent2D      renderExtent() const { return m_renderExtent; }

    VkImage     image(TextureHandle texture) const;
    VkImageView view(TextureHandle texture) const;
    VkExtent2D  extent(TextureHandle texture) const;
    VkBuffer    buffer(BufferHandle buffer) const;
    uint32_t    sampledIndex(TextureHandle texture) const;

private:
    friend class RenderGraph;
    PassContext(const RenderGraph &graph, VkCommandBuffer cmd, uint32_t pass, VkExtent2D renderExtent)
        : m_graph(graph), m_cmd(cmd), m_pass(pass), m_renderExtent(renderExtent) {}

    const ResourceDecl &declared(Kind kind, uint32_t resource) const;

    const RenderGraph &m_graph;
    VkCommandBuffer    m_cmd;
    uint32_t           m_pass;
    VkExtent2D         m_renderExtent;
};

class RenderGraph
{
public:
    void init(gfx::Context &ctx, ResourceStore &store, uint32_t framesInFlight);
    void shutdown();

    void beginFrame(VkExtent2D swapchainExtent);

    TextureHandle create(std::string_view name, const TextureDesc &desc);
    TextureHandle import(std::string_view name, const gfx::Image &image, ResourceState initial, ResourceState final);
    BufferHandle  import(std::string_view name, VkBuffer buffer, ResourceState initial);

    PassBuilder addPass(std::string_view name);

    VkExtent2D swapchainExtent() const { return m_swapchainExtent; }
    VkExtent2D extentOf(TextureHandle texture) const;

    std::span<const PassDecl>     passes() const    { return { m_passes.data(), m_passCount }; }
    std::span<const ResourceDecl> resources() const { return { m_resources.data(), m_resourceCount }; }

    void compile();
    void execute(VkCommandBuffer cmd, gfx::GpuProfiler &profiler);

    const BarrierPlan &plan() const { return m_plan; }

    std::string dump() const;
    bool        writeGraphviz(const std::filesystem::path &path) const;

private:
    friend class PassBuilder;
    friend class PassContext;

    struct PhysicalTexture
    {
        std::string       name;
        VkFormat          format = VK_FORMAT_UNDEFINED;
        VkExtent2D        extent{};
        VkImageUsageFlags usage = 0;
        gfx::Image        image;
        uint32_t          sampledSlot = 0;
        Tracked           state;
        uint64_t          lastUsedFrame = 0;
    };

    ResourceDecl       &newResource(Kind kind, std::string_view name);
    const ResourceDecl &resource(Kind kind, uint32_t index, std::string_view pass) const;
    void                requireDeclaring(std::string_view what) const;

    const char *label(std::string_view name);
    VkExtent2D  renderExtent(const PassDecl &pass) const;
    bool        usedAfter(uint32_t pass, uint32_t resource) const;

    void realize();
    void allocate(PhysicalTexture &physical, const ResourceDecl &decl, VkImageUsageFlags usage);
    void release(PhysicalTexture &physical);
    void releaseUnused();

    VkRenderingAttachmentInfo attachmentInfo(uint32_t pass, const AttachmentDecl &attachment) const;
    void beginRendering(VkCommandBuffer cmd, uint32_t pass);
    void recordBarriers(VkCommandBuffer cmd, BarrierRange range);

    gfx::Context  *m_ctx            = nullptr;
    ResourceStore *m_store          = nullptr;
    uint32_t       m_framesInFlight = 0;
    uint64_t       m_frame          = 0;

    VkExtent2D                m_swapchainExtent{};
    std::vector<ResourceDecl> m_resources;
    std::vector<PassDecl>     m_passes;
    size_t                    m_resourceCount = 0;
    size_t                    m_passCount     = 0;
    bool                      m_compiled      = false;

    BarrierPlan                            m_plan;
    std::vector<Tracked>                   m_initial;
    std::vector<VkExtent2D>                m_renderExtents;
    std::vector<const char *>              m_labels;
    std::set<std::string, std::less<>>     m_labelPool;
    std::vector<VkImageMemoryBarrier2>     m_imageBarriers;
    std::vector<VkBufferMemoryBarrier2>    m_bufferBarriers;
    std::vector<VkRenderingAttachmentInfo> m_colorInfos;

    std::vector<PhysicalTexture>   m_physical;
    std::vector<VkImageUsageFlags> m_usage;
    std::vector<uint32_t>          m_physicalOf;
};

}
