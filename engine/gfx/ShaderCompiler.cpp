#include "ShaderCompiler.h"

#include <shaderc/shaderc.hpp>

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>

#include "Context.h"
#include "core/Hash.h"

namespace gfx
{

namespace
{

std::optional<std::string> readText(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

std::optional<shaderc_shader_kind> stageFor(const std::filesystem::path &path)
{
    const std::string extension = path.extension().string();
    if (extension == ".vert") {
        return shaderc_glsl_vertex_shader;
    }
    if (extension == ".frag") {
        return shaderc_glsl_fragment_shader;
    }
    if (extension == ".comp") {
        return shaderc_glsl_compute_shader;
    }
    return std::nullopt;
}

constexpr uint32_t ShaderCacheVersion = 1;
constexpr uint32_t SpirvMagic         = 0x07230203; // Every SpirV module starts with this number. So we check


struct CompileSettings
{
    shaderc_env_version        environment = shaderc_env_version_vulkan_1_3;
    shaderc_spirv_version      spirv       = shaderc_spirv_version_1_6;
#ifndef NDEBUG
    bool                       debugInfo    = true;   // lets RenderDoc show the GLSL source
    shaderc_optimization_level optimization = shaderc_optimization_level_zero;
#else
    bool                       debugInfo    = false;
    shaderc_optimization_level optimization = shaderc_optimization_level_performance;
#endif
};
constexpr CompileSettings Settings{};

shaderc::CompileOptions makeOptions()
{
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, Settings.environment);
    options.SetTargetSpirv(Settings.spirv);
    if (Settings.debugInfo) {
        options.SetGenerateDebugInfo();
    }
    options.SetOptimizationLevel(Settings.optimization);
    return options;
}

uint64_t cacheKey(shaderc_shader_kind kind, std::string_view preprocessed)
{
    const std::string settings = std::format("v{} kind{} env{} spv{} debug{} opt{}", ShaderCacheVersion,
                                             static_cast<int>(kind), static_cast<int>(Settings.environment),
                                             static_cast<int>(Settings.spirv), Settings.debugInfo,
                                             static_cast<int>(Settings.optimization));
    return core::fnv1a64(preprocessed, core::fnv1a64(settings));
}

std::optional<std::vector<uint32_t>> loadCached(const std::filesystem::path &path)
{
    const std::optional<std::string> bytes = readText(path);
    if (!bytes || bytes->empty() || bytes->size() % sizeof(uint32_t) != 0) {
        return std::nullopt;
    }
    std::vector<uint32_t> words(bytes->size() / sizeof(uint32_t));
    std::memcpy(words.data(), bytes->data(), bytes->size());
    if (words[0] != SpirvMagic) {
        return std::nullopt;
    }
    return words;
}

bool writeCached(const std::filesystem::path &path, std::span<const uint32_t> words)
{
    std::random_device random;
    const std::filesystem::path temp = path.string() + std::format(".{:08x}{:08x}.tmp", random(), random());
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char *>(words.data()), static_cast<std::streamsize>(words.size_bytes()));
        if (!file) {
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

class FileIncluder final : public shaderc::CompileOptions::IncluderInterface
{
public:
    FileIncluder(const std::vector<std::filesystem::path> &roots, std::vector<std::filesystem::path> &opened)
        : m_roots(roots), m_opened(opened) {}

    shaderc_include_result *GetInclude(const char *requested, shaderc_include_type type,
                                       const char *requesting, size_t) override
    {
        auto *entry = new Entry;

        if (const std::optional<std::filesystem::path> path = resolve(requested, type, requesting)) {
            if (std::optional<std::string> text = readText(*path)) {
                entry->name = path->string();
                entry->content = std::move(*text);
                m_opened.push_back(*path);
            }
        }
        if (entry->name.empty()) {
            entry->content = std::format("Cannot open include {}", requested);
        }

        entry->result = shaderc_include_result
        {
            .source_name = entry->name.data(),
            .source_name_length = entry->name.size(),
            .content = entry->content.data(),
            .content_length = entry->content.size(),
            .user_data = entry
        };
        return &entry->result;
    }

    void ReleaseInclude(shaderc_include_result *result) override
    {
        delete static_cast<Entry *>(result->user_data);
    }

private:
    struct Entry
    {
        std::string name;
        std::string content;
        shaderc_include_result result{};
    };

    std::optional<std::filesystem::path> resolve(const char *requested, shaderc_include_type type,
                                                 const char *requesting) const
    {
        std::error_code ec;
        if (type == shaderc_include_type_relative) {
            const std::filesystem::path local = std::filesystem::path(requesting).parent_path() / requested;
            if (std::filesystem::is_regular_file(local, ec)) {
                return std::filesystem::weakly_canonical(local, ec);
            }
        }
        for (const std::filesystem::path &root : m_roots) {
            const std::filesystem::path candidate = root / requested;
            if (std::filesystem::is_regular_file(candidate, ec)) {
                return std::filesystem::weakly_canonical(candidate, ec);
            }
        }
        return std::nullopt;
    }

    const std::vector<std::filesystem::path> &m_roots;
    std::vector<std::filesystem::path>       &m_opened;
};

}

void ShaderCompiler::init(std::vector<std::filesystem::path> includeRoots, std::filesystem::path cacheDir)
{
    m_includeRoots = std::move(includeRoots);
    m_cacheDir = std::move(cacheDir);

    std::error_code ec;
    if (!m_cacheDir.empty() && !std::filesystem::create_directories(m_cacheDir, ec) && ec) {
        core::warn(std::format("SPIR-V cache disabled, cannot create {}: {}", m_cacheDir.string(), ec.message()));
        m_cacheDir.clear();
    }
}

ShaderBinary ShaderCompiler::compile(const std::filesystem::path &path) const
{
    ShaderBinary binary;

    const std::optional<std::string> source = readText(path);
    if (!source) {
        binary.error = std::format("Cannot open shader {}", path.string());
        return binary;
    }
    const std::optional<shaderc_shader_kind> kind = stageFor(path);
    if (!kind) {
        binary.error = std::format("Unknown shader stage for {}", path.string());
        return binary;
    }

    shaderc::CompileOptions options = makeOptions();
    options.SetIncluder(std::make_unique<FileIncluder>(m_includeRoots, binary.includes));

    const shaderc::Compiler compiler;
    const std::string name = path.string();

    const shaderc::PreprocessedSourceCompilationResult preprocessed =
        compiler.PreprocessGlsl(*source, *kind, name.c_str(), options);
    if (preprocessed.GetCompilationStatus() != shaderc_compilation_status_success) {
        binary.error = preprocessed.GetErrorMessage();
        return binary;
    }
    const std::string text(preprocessed.cbegin(), preprocessed.cend());

    std::ranges::sort(binary.includes);
    const auto [first, last] = std::ranges::unique(binary.includes);
    binary.includes.erase(first, last);

    std::filesystem::path cachePath;
    if (!m_cacheDir.empty()) {
        cachePath = m_cacheDir / std::format("{:016x}.spv", cacheKey(*kind, text));
        if (std::optional<std::vector<uint32_t>> cached = loadCached(cachePath)) {
            binary.spirv = std::move(*cached);
            binary.cacheHit = true;
            return binary;
        }
    }

    const shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(text, *kind, name.c_str(), options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        binary.error = result.GetErrorMessage();
        return binary;
    }
    binary.spirv.assign(result.cbegin(), result.cend());

    if (!cachePath.empty() && !writeCached(cachePath, binary.spirv) && !m_warnedCacheWrite) {
        core::warn(std::format("Cannot write SPIR-V cache file {}", cachePath.string()));
        m_warnedCacheWrite = true;
    }
    return binary;
}

VkShaderModule createShaderModule(const Context &ctx, std::span<const uint32_t> spirv, const char *name)
{
    const VkShaderModuleCreateInfo createInfo
    {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirv.size_bytes(),
        .pCode = spirv.data()
    };
    VkShaderModule module = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(ctx.device(), &createInfo, nullptr, &module));
    ctx.setName(VK_OBJECT_TYPE_SHADER_MODULE, module, name);
    return module;
}

}
