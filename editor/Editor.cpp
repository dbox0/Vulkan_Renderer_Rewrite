#include "Editor.h"

#include <SDL3/SDL_dialog.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cmath>

#include "Application.h"

namespace
{

// ImGui's colours are authored in sRGB. The swapchain is an sRGB format and would encode them a second time.
void linearizeColors(ImGuiStyle &style)
{
    for (ImVec4 &c : style.Colors) {
        c.x = std::pow(c.x, 2.2f);
        c.y = std::pow(c.y, 2.2f);
        c.z = std::pow(c.z, 2.2f);
    }
}

}

void Editor::attach(Application &app)
{
    m_app = &app;
    gfx::Context &ctx = app.context();

    const VkDescriptorPoolSize poolSize
    {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 16
    };
    const VkDescriptorPoolCreateInfo poolInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 16,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };
    VK_CHECK(vkCreateDescriptorPool(ctx.device(), &poolInfo, nullptr, &m_pool));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = "editor_layout.ini";
    ImGui::StyleColorsDark();
    linearizeColors(ImGui::GetStyle());

    if (!ImGui_ImplSDL3_InitForVulkan(app.window())) {
        core::fatal("ImGui_ImplSDL3_InitForVulkan failed");
    }
    ImGui_ImplVulkan_LoadFunctions(
        gfx::Context::ApiVersion,
        [](const char *name, void *userData) {
            return vkGetInstanceProcAddr(static_cast<VkInstance>(userData), name);
        },
        ctx.instance());

    const VkFormat colorFormat = gfx::Swapchain::Format;
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = gfx::Context::ApiVersion;
    initInfo.Instance = ctx.instance();
    initInfo.PhysicalDevice = ctx.physicalDevice();
    initInfo.Device = ctx.device();
    initInfo.QueueFamily = ctx.queueFamily();
    initInfo.Queue = ctx.queue().handle();
    initInfo.DescriptorPool = m_pool;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = std::max(2u, app.swapchain().imageCount());
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineRenderingCreateInfo = VkPipelineRenderingCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat
    };
    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        core::fatal("ImGui_ImplVulkan_Init failed");
    }

    app.renderer().setOverlay([](VkCommandBuffer cmd) {
        if (ImDrawData *drawData = ImGui::GetDrawData()) {
            ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
        }
    });
    app.setLayer(this);
}

void Editor::detach()
{
    if (!m_app) {
        return;
    }
    VK_CHECK(vkDeviceWaitIdle(m_app->context().device()));

    m_app->renderer().setOverlay(nullptr);
    m_app->setLayer(nullptr);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    vkDestroyDescriptorPool(m_app->context().device(), m_pool, nullptr);
    m_pool = VK_NULL_HANDLE;
    m_app = nullptr;
}

void Editor::openModel(const std::filesystem::path &path)
{
    std::lock_guard lock(m_pendingMutex);
    m_pendingModel = path;
}

bool Editor::onEvent(const SDL_Event &event)
{
    ImGui_ImplSDL3_ProcessEvent(&event);

    if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) {
        openModel(event.drop.data);
        return true;
    }

    const ImGuiIO &io = ImGui::GetIO();
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
        return io.WantCaptureKeyboard;
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
        return io.WantCaptureMouse;
    default:
        return false;
    }
}

bool Editor::wantsKeyboard() const
{
    return ImGui::GetIO().WantCaptureKeyboard;
}

void Editor::onUpdate(float)
{
    std::optional<std::filesystem::path> pending;
    {
        std::lock_guard lock(m_pendingMutex);
        pending.swap(m_pendingModel);
    }
    if (pending) {
        m_currentModel = m_app->loadData(*pending) ? *pending : std::filesystem::path{};
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) {
        showOpenDialog();
    }
    drawMenuBar();
    drawScenePanel();

    ImGui::Render();
}

void Editor::showOpenDialog()
{
    static const SDL_DialogFileFilter filters[] = { { "glTF", "gltf;glb" } };

    SDL_ShowOpenFileDialog(
        [](void *userData, const char *const *files, int) {
            if (!files) {
                core::warn(std::string("File dialog failed: ") + SDL_GetError());
            } else if (files[0]) {
                static_cast<Editor *>(userData)->openModel(files[0]);
            }
        },
        this, m_app->window(), filters, 1, nullptr, false);
}

void Editor::drawMenuBar()
{
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open glTF...", "Ctrl+O")) {
            showOpenDialog();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Esc")) {
            m_app->quit();
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void Editor::drawScenePanel()
{
    ImGui::Begin("Scene");

    if (m_currentModel.empty()) {
        ImGui::TextUnformatted("No model loaded");
    } else {
        ImGui::TextUnformatted(m_currentModel.filename().string().c_str());
        ImGui::SetItemTooltip("%s", m_currentModel.string().c_str());
    }
    if (ImGui::Button("Open glTF...")) {
        showOpenDialog();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("or drop a file on the window");

    ImGui::SeparatorText("Model");
    ImGui::Text("%zu meshes, %zu materials", m_app->geometry().meshCount(), m_app->resources().materialCount());

    Scene &scene = m_app->scene();
    if (const uint32_t rootId = scene.rootNodeId()) {
        Node &root = scene.getNode(rootId);

        glm::vec3 position = root.getTranslation();
        if (ImGui::DragFloat3("Root position", &position.x, 0.05f)) {
            root.setTranslation(position);
        }
        float scale = root.getScale().x;
        if (ImGui::DragFloat("Root scale", &scale, 0.001f, 0.0001f, 1000.0f, "%.4f", ImGuiSliderFlags_Logarithmic)) {
            root.setScale(glm::vec3(scale));
        }
    }

    ImGui::SeparatorText("Camera");
    Camera &camera = m_app->camera();
    ImGui::SliderFloat("FOV", &camera.fovDegrees, 20.0f, 120.0f, "%.0f deg");
    ImGui::DragFloat("Near", &camera.nearPlane, 0.001f, 0.0001f, camera.farPlane, "%.4f");
    ImGui::DragFloat("Far", &camera.farPlane, 1.0f, camera.nearPlane, 100000.0f, "%.0f");
    ImGui::DragFloat("Speed", &camera.speed, 0.1f, 0.01f, 1000.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    if (ImGui::Button("Look at origin")) {
        camera.lookAt(glm::vec3(0.0f));
    }
    ImGui::TextDisabled("Hold RMB: look, WASD/QE fly. MMB drag: pan");

    ImGui::SeparatorText("Frame");
    const core::FrameStats &stats = m_app->frameStats();
    const float average = stats.average();
    ImGui::Text("%.2f ms avg, %.2f ms max (%.0f FPS)", average, stats.maximum(), average > 0.0f ? 1000.0f / average : 0.0f);
    ImGui::PlotLines("##frametimes", stats.data(), static_cast<int>(stats.count()), static_cast<int>(stats.oldest()),
                     nullptr, 0.0f, std::max(stats.maximum(), 1.0f) * 1.2f, ImVec2(-1.0f, 60.0f));

    ImGui::SeparatorText("GPU");
    for (const gfx::GpuProfiler::Result &result : m_app->renderer().gpuProfiler().results()) {
        ImGui::Text("%s: %.3f ms", result.name, result.milliseconds);
    }

    gfx::Swapchain &swapchain = m_app->swapchain();
    if (ImGui::BeginCombo("Present mode", gfx::presentModeName(swapchain.presentMode()))) {
        for (const VkPresentModeKHR mode : swapchain.supportedPresentModes()) {
            if (ImGui::Selectable(gfx::presentModeName(mode), mode == swapchain.presentMode())) {
                swapchain.setPresentMode(mode);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("FIFO is capped at the refresh rate");

    ImGui::End();
}
