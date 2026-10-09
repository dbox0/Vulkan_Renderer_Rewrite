#include "RenderGraph.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <span>
#include <utility>

namespace render::rg
{

namespace
{

template <typename Flags>
struct FlagName
{
    Flags       bit;
    const char *name;
};

constexpr FlagName<VkPipelineStageFlags2> StageNames[]
{
    { VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,             "TOP_OF_PIPE" },
    { VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,           "DRAW_INDIRECT" },
    { VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,            "VERTEX_INPUT" },
    { VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,           "VERTEX_SHADER" },
    { VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,         "FRAGMENT_SHADER" },
    { VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,    "EARLY_FRAGMENT_TESTS" },
    { VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,     "LATE_FRAGMENT_TESTS" },
    { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, "COLOR_ATTACHMENT_OUTPUT" },
    { VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,          "COMPUTE_SHADER" },
    { VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,            "ALL_TRANSFER" },
    { VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,          "BOTTOM_OF_PIPE" },
    { VK_PIPELINE_STAGE_2_HOST_BIT,                    "HOST" },
    { VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,            "ALL_GRAPHICS" },
    { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,            "ALL_COMMANDS" },
    { VK_PIPELINE_STAGE_2_COPY_BIT,                    "COPY" },
    { VK_PIPELINE_STAGE_2_RESOLVE_BIT,                 "RESOLVE" },
    { VK_PIPELINE_STAGE_2_BLIT_BIT,                    "BLIT" },
    { VK_PIPELINE_STAGE_2_CLEAR_BIT,                   "CLEAR" },
};

constexpr FlagName<VkAccessFlags2> AccessNames[]
{
    { VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,          "INDIRECT_COMMAND_READ" },
    { VK_ACCESS_2_INDEX_READ_BIT,                     "INDEX_READ" },
    { VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,          "VERTEX_ATTRIBUTE_READ" },
    { VK_ACCESS_2_UNIFORM_READ_BIT,                   "UNIFORM_READ" },
    { VK_ACCESS_2_SHADER_READ_BIT,                    "SHADER_READ" },
    { VK_ACCESS_2_SHADER_WRITE_BIT,                   "SHADER_WRITE" },
    { VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,          "COLOR_ATTACHMENT_READ" },
    { VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,         "COLOR_ATTACHMENT_WRITE" },
    { VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,  "DEPTH_STENCIL_ATTACHMENT_READ" },
    { VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, "DEPTH_STENCIL_ATTACHMENT_WRITE" },
    { VK_ACCESS_2_TRANSFER_READ_BIT,                  "TRANSFER_READ" },
    { VK_ACCESS_2_TRANSFER_WRITE_BIT,                 "TRANSFER_WRITE" },
    { VK_ACCESS_2_HOST_READ_BIT,                      "HOST_READ" },
    { VK_ACCESS_2_HOST_WRITE_BIT,                     "HOST_WRITE" },
    { VK_ACCESS_2_MEMORY_READ_BIT,                    "MEMORY_READ" },
    { VK_ACCESS_2_MEMORY_WRITE_BIT,                   "MEMORY_WRITE" },
    { VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,            "SHADER_SAMPLED_READ" },
    { VK_ACCESS_2_SHADER_STORAGE_READ_BIT,            "SHADER_STORAGE_READ" },
    { VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,           "SHADER_STORAGE_WRITE" },
};

template <typename Flags>
std::string flagNames(Flags flags, std::span<const FlagName<Flags>> names)
{
    if (flags == 0) {
        return "NONE";
    }
    std::string out;
    for (const FlagName<Flags> &entry : names) {
        if ((flags & entry.bit) == entry.bit) {
            out += out.empty() ? "" : "|";
            out += entry.name;
            flags &= ~entry.bit;
        }
    }
    if (flags != 0) {
        out += std::format("{}{:#x}", out.empty() ? "" : "|", static_cast<uint64_t>(flags));
    }
    return out;
}

std::string stageNames(VkPipelineStageFlags2 stages)
{
    return flagNames<VkPipelineStageFlags2>(stages, StageNames);
}

std::string accessNames(VkAccessFlags2 access)
{
    return flagNames<VkAccessFlags2>(access, AccessNames);
}

std::string layoutName(VkImageLayout layout)
{
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:                        return "UNDEFINED";
    case VK_IMAGE_LAYOUT_GENERAL:                          return "GENERAL";
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:         return "COLOR_ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return "DEPTH_STENCIL_ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:         return "SHADER_READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:             return "TRANSFER_SRC_OPTIMAL";
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:             return "TRANSFER_DST_OPTIMAL";
    case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:         return "DEPTH_ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:          return "DEPTH_READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL:                return "READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL:               return "ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:                  return "PRESENT_SRC";
    default:                                               return std::format("layout {}", static_cast<int>(layout));
    }
}

std::string formatName(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:      return "R8G8B8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB:       return "R8G8B8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_UNORM:      return "B8G8R8A8_UNORM";
    case VK_FORMAT_B8G8R8A8_SRGB:       return "B8G8R8A8_SRGB";
    case VK_FORMAT_R16G16B16A16_SFLOAT: return "R16G16B16A16_SFLOAT";
    case VK_FORMAT_R32G32B32A32_SFLOAT: return "R32G32B32A32_SFLOAT";
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return "B10G11R11_UFLOAT";
    case VK_FORMAT_D16_UNORM:           return "D16_UNORM";
    case VK_FORMAT_D32_SFLOAT:          return "D32_SFLOAT";
    case VK_FORMAT_D24_UNORM_S8_UINT:   return "D24_UNORM_S8_UINT";
    default:                            return std::format("format {}", static_cast<int>(format));
    }
}

const char *loadName(LoadOp load)
{
    switch (load) {
    case LoadOp::Clear: return "Clear";
    case LoadOp::Load:  return "Load";
    default:            return "DontCare";
    }
}

const AttachmentDecl *attachmentFor(const PassDecl &pass, uint32_t resource)
{
    for (const AttachmentDecl &color : pass.colors) {
        if (color.texture.index == resource) {
            return &color;
        }
    }
    if (pass.depth && pass.depth->texture.index == resource) {
        return &*pass.depth;
    }
    return nullptr;
}

std::string dotString(std::string_view text)
{
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

}

std::string RenderGraph::dump() const
{
    const std::span<const ResourceDecl> declared = resources();
    size_t width = 0;
    for (const ResourceDecl &decl : declared) {
        width = std::max(width, decl.name.size());
    }

    const auto barrierLine = [&](const BarrierRecord &barrier) {
        const ResourceDecl &decl = declared[barrier.resource];
        std::string line = std::format("        barrier {:<{}} {}/{}", decl.name, width,
                                       stageNames(barrier.src.stage), accessNames(barrier.src.access));
        if (barrier.kind == Kind::Texture) {
            line += " " + layoutName(barrier.src.layout);
        }
        line += std::format(" -> {}/{}", stageNames(barrier.dst.stage), accessNames(barrier.dst.access));
        if (barrier.kind == Kind::Texture) {
            line += " " + layoutName(barrier.dst.layout);
        }
        return line + "\n";
    };
    const auto barrierLines = [&](BarrierRange range) {
        std::string lines;
        for (uint32_t i = range.first; i < range.first + range.count; ++i) {
            lines += barrierLine(m_plan.barriers[i]);
        }
        return lines;
    };

    std::string out = std::format("render graph, frame {}, swapchain {}x{}{}\n", m_frame, m_swapchainExtent.width,
                                  m_swapchainExtent.height, m_compiled ? "" : " (not compiled)");

    out += "resources\n";
    for (size_t r = 0; r < declared.size(); ++r) {
        const ResourceDecl &decl = declared[r];
        out += std::format("  [{}] {:<{}} {:<9}", r, decl.name, width, decl.imported ? "imported" : "transient");
        if (decl.kind == Kind::Texture) {
            out += std::format(" {} {}x{}", formatName(decl.format), decl.extent.width, decl.extent.height);
            if (decl.sampledSlot != 0) {
                out += std::format(" slot {}", decl.sampledSlot);
            }
        } else {
            out += " buffer";
        }
        out += "\n";
    }

    out += "passes\n";
    for (size_t p = 0; p < m_passCount; ++p) {
        const PassDecl &pass = m_passes[p];
        out += std::format("  [{}] {}", p, pass.name);
        if (m_compiled && (!pass.colors.empty() || pass.depth)) {
            out += std::format("  {}x{}", m_renderExtents[p].width, m_renderExtents[p].height);
        }
        out += "\n";
        if (m_compiled) {
            out += barrierLines(m_plan.passes[p]);
        }
        for (const UseDecl &use : pass.uses) {
            out += std::format("        {:<7} {:<{}} {}", writes(use.usage) ? "write" : "read",
                               declared[use.resource].name, width, usageName(use.usage));
            if (const AttachmentDecl *attachment = attachmentFor(pass, use.resource)) {
                out += std::format(" {}", loadName(attachment->load));
            }
            out += "\n";
        }
    }

    if (m_compiled && m_plan.final.count > 0) {
        out += "final\n" + barrierLines(m_plan.final);
    }
    return out;
}

bool RenderGraph::writeGraphviz(const std::filesystem::path &path) const
{
    std::ofstream file(path);
    if (!file) {
        return false;
    }

    const std::span<const ResourceDecl> declared = resources();
    std::vector<uint32_t> version(declared.size(), 0);
    std::vector<std::pair<uint32_t, uint32_t>> emitted;

    const auto node = [&](uint32_t resource) {
        const std::string id = std::format("r{}v{}", resource, version[resource]);
        const std::pair key{ resource, version[resource] };
        if (std::ranges::find(emitted, key) == emitted.end()) {
            emitted.push_back(key);
            const ResourceDecl &decl = declared[resource];
            file << std::format("  {} [shape=ellipse, label={}{}];\n", id, dotString(decl.name),
                                decl.imported ? ", style=dashed" : "");
        }
        return id;
    };

    file << "digraph RenderGraph {\n  rankdir=LR;\n  node [fontname=\"Helvetica\", fontsize=10];\n"
            "  edge [fontname=\"Helvetica\", fontsize=8];\n";
    for (size_t p = 0; p < m_passCount; ++p) {
        const PassDecl &pass = m_passes[p];
        file << std::format("  p{} [shape=box, style=filled, fillcolor=\"#dde6f5\", label={}];\n", p, dotString(pass.name));
        for (const UseDecl &use : pass.uses) {
            if (loadsContents(pass, use)) {
                const std::string from = node(use.resource);
                file << std::format("  {} -> p{} [label={}];\n", from, p, dotString(usageName(use.usage)));
            }
            if (writes(use.usage)) {
                ++version[use.resource];
                const std::string to = node(use.resource);
                file << std::format("  p{} -> {} [label={}];\n", p, to, dotString(usageName(use.usage)));
            }
        }
    }
    file << "}\n";
    return static_cast<bool>(file);
}

}
