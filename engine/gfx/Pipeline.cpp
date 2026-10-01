#include "Pipeline.h"

#include <shaderc/shaderc.hpp>

#include <array>
#include <fstream>
#include <sstream>

#include "Context.h"

namespace gfx
{

VkShaderModule loadShader(const Context &ctx, const std::filesystem::path &path)
{
    std::ifstream file(path);
    if (!file) {
        core::fatal(std::format("Cannot open shader {}", path.string()));
    }
    std::stringstream source;
    source << file.rdbuf();

    const std::string extension = path.extension().string();
    shaderc_shader_kind kind = shaderc_glsl_vertex_shader;
    if (extension == ".frag") {
        kind = shaderc_glsl_fragment_shader;
    } else if (extension == ".comp") {
        kind = shaderc_glsl_compute_shader;
    } else if (extension != ".vert") {
        core::fatal(std::format("Unknown shader stage for {}", path.string()));
    }

    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    options.SetTargetSpirv(shaderc_spirv_version_1_6);
#ifndef NDEBUG
    options.SetGenerateDebugInfo();   // lets RenderDoc show the GLSL source
#else
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
#endif

    shaderc::Compiler compiler;
    const shaderc::SpvCompilationResult result =
        compiler.CompileGlslToSpv(source.str(), kind, path.string().c_str(), options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        core::fatal(std::format("Shader compilation failed:\n{}", result.GetErrorMessage()));
    }

    const std::vector<uint32_t> spirv(result.cbegin(), result.cend());
    const VkShaderModuleCreateInfo createInfo
    {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirv.size() * sizeof(uint32_t),
        .pCode = spirv.data()
    };
    VkShaderModule module = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(ctx.device(), &createInfo, nullptr, &module));
    return module;
}

VkPipeline createGraphicsPipeline(const Context &ctx, const GraphicsPipelineDesc &desc, const char *name)
{
    const std::array stages
    {
        VkPipelineShaderStageCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = desc.vertex,
            .pName = "main"
        },
        VkPipelineShaderStageCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = desc.fragment,
            .pName = "main"
        }
    };

    // Vertices are pulled from buffers in the shader, so there is no vertex input state.
    const VkPipelineVertexInputStateCreateInfo vertexInput{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    const VkPipelineInputAssemblyStateCreateInfo inputAssembly
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = desc.topology
    };

    const VkPipelineViewportStateCreateInfo viewport
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
    };

    const VkPipelineRasterizationStateCreateInfo rasterization
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = desc.cullMode,
        .frontFace = desc.frontFace,
        .lineWidth = 1.0f
    };

    const VkPipelineMultisampleStateCreateInfo multisample
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };

    const VkPipelineDepthStencilStateCreateInfo depthStencil
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = desc.depthTest ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = desc.depthWrite ? VK_TRUE : VK_FALSE,
        .depthCompareOp = desc.depthCompare
    };

    const std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(
        desc.colorFormats.size(),
        VkPipelineColorBlendAttachmentState
        {
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        });
    const VkPipelineColorBlendStateCreateInfo blend
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = static_cast<uint32_t>(blendAttachments.size()),
        .pAttachments = blendAttachments.data()
    };

    const std::array dynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    const VkPipelineDynamicStateCreateInfo dynamic
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    const VkPipelineRenderingCreateInfo rendering
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = static_cast<uint32_t>(desc.colorFormats.size()),
        .pColorAttachmentFormats = desc.colorFormats.data(),
        .depthAttachmentFormat = desc.depthFormat
    };

    const VkGraphicsPipelineCreateInfo createInfo
    {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = static_cast<uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewport,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = desc.layout
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(ctx.device(), VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline));
    ctx.setName(VK_OBJECT_TYPE_PIPELINE, pipeline, name);
    return pipeline;
}

}
