#include "RenderGraph.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "core/Log.h"
#include "gfx/Context.h"
#include "gfx/GpuProfiler.h"
#include "render/ResourceStore.h"

namespace render::rg
{

namespace
{

const char *kindName(Kind kind)
{
    return kind == Kind::Texture ? "texture" : "buffer";
}

VkAttachmentLoadOp vkLoadOp(LoadOp load)
{
    switch (load) {
    case LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadOp::Load:  return VK_ATTACHMENT_LOAD_OP_LOAD;
    default:            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
}

VkImageAspectFlags aspectFor(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

bool hasAttachments(const PassDecl &pass)
{
    return !pass.colors.empty() || pass.depth.has_value();
}

Usage usageIn(const PassDecl &pass, uint32_t resource)
{
    for (const UseDecl &use : pass.uses) {
        if (use.kind == Kind::Texture && use.resource == resource) {
            return use.usage;
        }
    }
    core::fatal(std::format("Pass '{}' has an attachment it does not use", pass.name));
}

}

PassDecl &PassBuilder::pass()
{
    return m_graph.m_passes[m_pass];
}

PassBuilder &PassBuilder::use(Kind kind, uint32_t resource, Usage usage, bool expectWrite)
{
    PassDecl &decl = pass();
    const ResourceDecl &target = m_graph.resource(kind, resource, decl.name);

    if (!validFor(usage, kind)) {
        core::fatal(std::format("Pass '{}': {} is not a valid usage for {} '{}'",
                                decl.name, usageName(usage), kindName(kind), target.name));
    }
    if (writes(usage) != expectWrite) {
        core::fatal(std::format("Pass '{}': {} on '{}' is declared as a {} but it {}",
                                decl.name, usageName(usage), target.name, expectWrite ? "write" : "read",
                                writes(usage) ? "writes" : "only reads"));
    }
    const bool repeated = std::ranges::any_of(decl.uses, [&](const UseDecl &existing) {
        return existing.kind == kind && existing.resource == resource;
    });
    if (repeated) {
        core::fatal(std::format("Pass '{}' uses '{}' more than once", decl.name, target.name));
    }

    decl.uses.push_back(UseDecl{ .kind = kind, .resource = resource, .usage = usage });
    return *this;
}

PassBuilder &PassBuilder::color(TextureHandle target, LoadOp load, VkClearColorValue clear)
{
    use(Kind::Texture, target.index, load == LoadOp::Load ? Usage::ColorReadWrite : Usage::ColorWrite, true);
    pass().colors.push_back(AttachmentDecl{ .texture = target, .load = load, .clear{ .color = clear } });
    return *this;
}

PassBuilder &PassBuilder::depth(TextureHandle target, LoadOp load, float clear, bool write)
{
    PassDecl &decl = pass();
    if (decl.depth) {
        core::fatal(std::format("Pass '{}' declares a second depth attachment", decl.name));
    }
    if (!write && load == LoadOp::Clear) {
        core::fatal(std::format("Pass '{}' clears a read-only depth attachment", decl.name));
    }
    use(Kind::Texture, target.index, write ? Usage::DepthWrite : Usage::DepthRead, write);
    pass().depth = AttachmentDecl{ .texture = target, .load = load, .clear{ .depthStencil{ .depth = clear } } };
    return *this;
}

PassBuilder &PassBuilder::read(TextureHandle texture, Usage usage)
{
    return use(Kind::Texture, texture.index, usage, false);
}

PassBuilder &PassBuilder::write(TextureHandle texture, Usage usage)
{
    return use(Kind::Texture, texture.index, usage, true);
}

PassBuilder &PassBuilder::read(BufferHandle buffer, Usage usage)
{
    return use(Kind::Buffer, buffer.index, usage, false);
}

PassBuilder &PassBuilder::write(BufferHandle buffer, Usage usage)
{
    return use(Kind::Buffer, buffer.index, usage, true);
}

void PassBuilder::execute(std::function<void(PassContext &)> body)
{
    pass().body = std::move(body);
}

void RenderGraph::init(gfx::Context &ctx, ResourceStore &store, uint32_t framesInFlight)
{
    m_ctx = &ctx;
    m_store = &store;
    m_framesInFlight = framesInFlight;
}

void RenderGraph::shutdown()
{
    for (PhysicalTexture &physical : m_physical) {
        m_ctx->destroyImage(physical.image);
    }
    m_physical.clear();
}

void RenderGraph::beginFrame(VkExtent2D swapchainExtent)
{
    ++m_frame;
    m_swapchainExtent = swapchainExtent;
    m_resourceCount = 0;
    m_passCount = 0;
    m_compiled = false;
}

void RenderGraph::requireDeclaring(std::string_view what) const
{
    if (m_compiled) {
        core::fatal(std::format("Render graph: '{}' declared after compile; call beginFrame first", what));
    }
}

ResourceDecl &RenderGraph::newResource(Kind kind, std::string_view name)
{
    requireDeclaring(name);
    if (m_resourceCount == m_resources.size()) {
        m_resources.emplace_back();
    }
    ResourceDecl &decl = m_resources[m_resourceCount++];
    decl = ResourceDecl{};
    decl.kind = kind;
    decl.name = name;
    return decl;
}

const ResourceDecl &RenderGraph::resource(Kind kind, uint32_t index, std::string_view pass) const
{
    if (index >= m_resourceCount || m_resources[index].kind != kind) {
        core::fatal(std::format("Pass '{}' uses an invalid {} handle", pass, kindName(kind)));
    }
    return m_resources[index];
}

TextureHandle RenderGraph::create(std::string_view name, const TextureDesc &desc)
{
    if (desc.format == VK_FORMAT_UNDEFINED) {
        core::fatal(std::format("Render graph texture '{}' has no format", name));
    }
    if (desc.sizeMode == SizeMode::Absolute && (desc.extent.width == 0 || desc.extent.height == 0)) {
        core::fatal(std::format("Render graph texture '{}' has an absolute size of zero", name));
    }
    for (size_t r = 0; r < m_resourceCount; ++r) {
        const ResourceDecl &existing = m_resources[r];
        if (existing.kind == Kind::Texture && !existing.imported && existing.name == name) {
            core::fatal(std::format("Render graph texture '{}' is created twice in one frame", name));
        }
    }
    const auto index = static_cast<uint32_t>(m_resourceCount);
    ResourceDecl &decl = newResource(Kind::Texture, name);
    decl.desc = desc;
    decl.format = desc.format;
    decl.extent = extentOf(TextureHandle{ index });
    return TextureHandle{ index };
}

TextureHandle RenderGraph::import(std::string_view name, const gfx::Image &image, ResourceState initial, ResourceState final)
{
    const auto index = static_cast<uint32_t>(m_resourceCount);
    ResourceDecl &decl = newResource(Kind::Texture, name);
    decl.imported = true;
    decl.image = image.image;
    decl.view = image.view;
    decl.format = image.format;
    decl.extent = image.extent;
    decl.initial = initial;
    decl.final = final;
    return TextureHandle{ index };
}

BufferHandle RenderGraph::import(std::string_view name, VkBuffer buffer, ResourceState initial)
{
    const auto index = static_cast<uint32_t>(m_resourceCount);
    ResourceDecl &decl = newResource(Kind::Buffer, name);
    decl.imported = true;
    decl.buffer = buffer;
    decl.initial = initial;
    return BufferHandle{ index };
}

PassBuilder RenderGraph::addPass(std::string_view name)
{
    requireDeclaring(name);
    if (m_passCount == m_passes.size()) {
        m_passes.emplace_back();
    }
    PassDecl &decl = m_passes[m_passCount];
    decl.name = name;
    decl.uses.clear();
    decl.colors.clear();
    decl.depth.reset();
    decl.body = nullptr;
    return PassBuilder(*this, static_cast<uint32_t>(m_passCount++));
}

VkExtent2D RenderGraph::extentOf(TextureHandle texture) const
{
    const ResourceDecl &decl = m_resources[texture.index];
    if (decl.imported || decl.desc.sizeMode == SizeMode::Absolute) {
        return decl.imported ? decl.extent : decl.desc.extent;
    }
    const auto scaled = [&](uint32_t size) {
        return std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(size) * decl.desc.scale)));
    };
    return { scaled(m_swapchainExtent.width), scaled(m_swapchainExtent.height) };
}

const ResourceDecl &PassContext::declared(Kind kind, uint32_t resource) const
{
    const PassDecl &pass = m_graph.m_passes[m_pass];
    for (const UseDecl &use : pass.uses) {
        if (use.kind == kind && use.resource == resource) {
            return m_graph.m_resources[resource];
        }
    }
    const std::string_view name = resource < m_graph.m_resourceCount ? std::string_view(m_graph.m_resources[resource].name) : "?";
    core::fatal(std::format("Pass '{}' accesses {} '{}' without declaring it", pass.name, kindName(kind), name));
}

VkImage PassContext::image(TextureHandle texture) const
{
    return declared(Kind::Texture, texture.index).image;
}

VkImageView PassContext::view(TextureHandle texture) const
{
    return declared(Kind::Texture, texture.index).view;
}

VkExtent2D PassContext::extent(TextureHandle texture) const
{
    return declared(Kind::Texture, texture.index).extent;
}

VkBuffer PassContext::buffer(BufferHandle buffer) const
{
    return declared(Kind::Buffer, buffer.index).buffer;
}

uint32_t PassContext::sampledIndex(TextureHandle texture) const
{
    const ResourceDecl &decl = declared(Kind::Texture, texture.index);
    if (decl.sampledSlot == 0) {
        core::fatal(std::format("Render graph texture '{}' has no bindless slot", decl.name));
    }
    return decl.sampledSlot;
}

const char *RenderGraph::label(std::string_view name)
{
    auto it = m_labelPool.find(name);
    if (it == m_labelPool.end()) {
        it = m_labelPool.emplace(name).first;
    }
    return it->c_str();
}

VkExtent2D RenderGraph::renderExtent(const PassDecl &pass) const
{
    VkExtent2D extent{};
    bool       first = true;
    const auto check = [&](const AttachmentDecl &attachment) {
        const ResourceDecl &decl = m_resources[attachment.texture.index];
        if (first) {
            extent = decl.extent;
            first = false;
        } else if (decl.extent.width != extent.width || decl.extent.height != extent.height) {
            core::fatal(std::format("Pass '{}': attachment '{}' is {}x{}, the others are {}x{}", pass.name, decl.name,
                                    decl.extent.width, decl.extent.height, extent.width, extent.height));
        }
    };
    for (const AttachmentDecl &color : pass.colors) {
        check(color);
    }
    if (pass.depth) {
        check(*pass.depth);
    }
    return extent;
}

bool RenderGraph::usedAfter(uint32_t pass, uint32_t resource) const
{
    for (size_t p = pass + 1; p < m_passCount; ++p) {
        for (const UseDecl &use : m_passes[p].uses) {
            if (use.resource == resource) {
                return true;
            }
        }
    }
    return false;
}

void RenderGraph::compile()
{
    if (m_compiled) {
        core::fatal("Render graph compiled twice in one frame");
    }

    m_labels.resize(m_passCount);
    m_renderExtents.resize(m_passCount);
    for (size_t p = 0; p < m_passCount; ++p) {
        const PassDecl &pass = m_passes[p];
        if (!pass.body) {
            core::fatal(std::format("Pass '{}' has no execute body", pass.name));
        }
        m_labels[p] = label(pass.name);
        m_renderExtents[p] = renderExtent(pass);
    }

    m_usage.assign(m_resourceCount, 0);
    for (size_t p = 0; p < m_passCount; ++p) {
        for (const UseDecl &use : m_passes[p].uses) {
            if (use.kind == Kind::Texture) {
                m_usage[use.resource] |= imageUsageFor(use.usage);
            }
        }
    }

    m_initial.resize(m_resourceCount);
    for (size_t r = 0; r < m_resourceCount; ++r) {
        const ResourceDecl &decl = m_resources[r];
        m_initial[r] = decl.imported ? trackedFrom(decl.initial) : Tracked{};
    }
    realize();

    compileBarriers(resources(), passes(), m_initial, m_plan);
    if (!m_plan.errors.empty()) {
        std::string message = "Render graph failed to compile:";
        for (const std::string &error : m_plan.errors) {
            message += "\n  " + error;
        }
        core::fatal(message);
    }

    for (size_t r = 0; r < m_resourceCount; ++r) {
        if (m_physicalOf[r] != UINT32_MAX) {
            m_physical[m_physicalOf[r]].state = m_plan.states[r];
        }
    }
    releaseUnused();
    m_compiled = true;
}

void RenderGraph::realize()
{
    if (!m_ctx) {
        core::fatal("Render graph used before init");
    }
    m_physicalOf.assign(m_resourceCount, UINT32_MAX);

    for (size_t r = 0; r < m_resourceCount; ++r) {
        ResourceDecl &decl = m_resources[r];
        if (decl.kind != Kind::Texture || decl.imported || m_usage[r] == 0) {
            continue;
        }

        auto it = std::ranges::find(m_physical, decl.name, &PhysicalTexture::name);
        if (it == m_physical.end()) {
            it = m_physical.insert(m_physical.end(), PhysicalTexture{ .name = decl.name });
        }
        PhysicalTexture &physical = *it;

        const bool fits = physical.image.image && physical.format == decl.format
            && physical.extent.width == decl.extent.width && physical.extent.height == decl.extent.height
            && (physical.usage & m_usage[r]) == m_usage[r];
        if (!fits) {
            release(physical);
            allocate(physical, decl, m_usage[r]);
        }
        physical.lastUsedFrame = m_frame;

        decl.image = physical.image.image;
        decl.view = physical.image.view;
        decl.sampledSlot = physical.sampledSlot;

        m_initial[r] = physical.state;
        m_initial[r].layout = VK_IMAGE_LAYOUT_UNDEFINED;
        m_physicalOf[r] = static_cast<uint32_t>(it - m_physical.begin());
    }
}

void RenderGraph::allocate(PhysicalTexture &physical, const ResourceDecl &decl, VkImageUsageFlags usage)
{
    const std::string debugName = std::format("rg:{} {}x{}", decl.name, decl.extent.width, decl.extent.height);
    physical.format = decl.format;
    physical.extent = decl.extent;
    physical.usage = usage;
    physical.image = m_ctx->createImage(decl.extent, decl.format, usage, debugName.c_str());
    physical.sampledSlot = (usage & VK_IMAGE_USAGE_SAMPLED_BIT) ? m_store->registerSampledView(physical.image.view) : 0;
    physical.state = Tracked{};
}

void RenderGraph::release(PhysicalTexture &physical)
{
    if (!physical.image.image) {
        return;
    }
    if (physical.sampledSlot != 0) {
        m_store->releaseSampledView(physical.sampledSlot);
    }
    m_ctx->retire(physical.image);
    physical.image = gfx::Image{};
    physical.sampledSlot = 0;
    physical.state = Tracked{};
}

void RenderGraph::releaseUnused()
{
    const uint64_t keepFor = 2ull * m_framesInFlight;
    for (size_t i = m_physical.size(); i-- > 0;) {
        if (m_physical[i].lastUsedFrame + keepFor < m_frame) {
            release(m_physical[i]);
            m_physical.erase(m_physical.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
}

void RenderGraph::recordBarriers(VkCommandBuffer cmd, BarrierRange range)
{
    if (range.count == 0) {
        return;
    }
    m_imageBarriers.clear();
    m_bufferBarriers.clear();

    for (uint32_t i = range.first; i < range.first + range.count; ++i) {
        const BarrierRecord &barrier = m_plan.barriers[i];
        const ResourceDecl  &decl = m_resources[barrier.resource];
        if (barrier.kind == Kind::Texture) {
            m_imageBarriers.push_back(VkImageMemoryBarrier2
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = barrier.src.stage,
                .srcAccessMask = barrier.src.access,
                .dstStageMask = barrier.dst.stage,
                .dstAccessMask = barrier.dst.access,
                .oldLayout = barrier.src.layout,
                .newLayout = barrier.dst.layout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = decl.image,
                .subresourceRange
                {
                    .aspectMask = aspectFor(decl.format),
                    .baseMipLevel = 0,
                    .levelCount = VK_REMAINING_MIP_LEVELS,
                    .baseArrayLayer = 0,
                    .layerCount = VK_REMAINING_ARRAY_LAYERS
                }
            });
        } else {
            m_bufferBarriers.push_back(VkBufferMemoryBarrier2
            {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = barrier.src.stage,
                .srcAccessMask = barrier.src.access,
                .dstStageMask = barrier.dst.stage,
                .dstAccessMask = barrier.dst.access,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = decl.buffer,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            });
        }
    }

    const VkDependencyInfo dependency
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = static_cast<uint32_t>(m_bufferBarriers.size()),
        .pBufferMemoryBarriers = m_bufferBarriers.data(),
        .imageMemoryBarrierCount = static_cast<uint32_t>(m_imageBarriers.size()),
        .pImageMemoryBarriers = m_imageBarriers.data()
    };
    vkCmdPipelineBarrier2(cmd, &dependency);
}

VkRenderingAttachmentInfo RenderGraph::attachmentInfo(uint32_t pass, const AttachmentDecl &attachment) const
{
    const uint32_t      resource = attachment.texture.index;
    const ResourceDecl &decl = m_resources[resource];
    const Usage         usage = usageIn(m_passes[pass], resource);

    VkAttachmentStoreOp store = VK_ATTACHMENT_STORE_OP_STORE;
    if (!writes(usage)) {
        store = VK_ATTACHMENT_STORE_OP_NONE;
    } else if (!decl.imported && !usedAfter(pass, resource)) {
        store = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }

    return VkRenderingAttachmentInfo
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = decl.view,
        .imageLayout = stateFor(usage).layout,
        .loadOp = vkLoadOp(attachment.load),
        .storeOp = store,
        .clearValue = attachment.clear
    };
}

void RenderGraph::beginRendering(VkCommandBuffer cmd, uint32_t pass)
{
    const PassDecl &decl = m_passes[pass];

    m_colorInfos.clear();
    for (const AttachmentDecl &color : decl.colors) {
        m_colorInfos.push_back(attachmentInfo(pass, color));
    }
    VkRenderingAttachmentInfo depthInfo{};
    if (decl.depth) {
        depthInfo = attachmentInfo(pass, *decl.depth);
    }

    const VkExtent2D extent = m_renderExtents[pass];
    const VkRenderingInfo info
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea{ .extent = extent },
        .layerCount = 1,
        .colorAttachmentCount = static_cast<uint32_t>(m_colorInfos.size()),
        .pColorAttachments = m_colorInfos.data(),
        .pDepthAttachment = decl.depth ? &depthInfo : nullptr
    };
    vkCmdBeginRendering(cmd, &info);

    const VkViewport viewport
    {
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    const VkRect2D scissor{ .extent = extent };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void RenderGraph::execute(VkCommandBuffer cmd, gfx::GpuProfiler &profiler)
{
    if (!m_compiled) {
        core::fatal("Render graph executed before compile");
    }

    for (uint32_t p = 0; p < m_passCount; ++p) {
        const PassDecl &pass = m_passes[p];
        recordBarriers(cmd, m_plan.passes[p]);

        const gfx::GpuProfiler::Scope scope(profiler, cmd, m_labels[p]);
        const bool rendering = hasAttachments(pass);
        if (rendering) {
            beginRendering(cmd, p);
        }
        PassContext ctx(*this, cmd, p, m_renderExtents[p]);
        pass.body(ctx);
        if (rendering) {
            vkCmdEndRendering(cmd);
        }
    }
    recordBarriers(cmd, m_plan.final);
}

}
