#include "Editor.h"

#include <SDL3/SDL_dialog.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <span>
#include <vector>

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

    constexpr uint32_t PoolSets = ResourceStore::MaxTextures + 16;
    const VkDescriptorPoolSize poolSize
    {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = PoolSets
    };
    const VkDescriptorPoolCreateInfo poolInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = PoolSets,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };
    VK_CHECK(vkCreateDescriptorPool(ctx.device(), &poolInfo, nullptr, &m_pool));

    const VkSamplerCreateInfo samplerInfo
    {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = VK_LOD_CLAMP_NONE
    };
    VK_CHECK(vkCreateSampler(ctx.device(), &samplerInfo, nullptr, &m_thumbnailSampler));
    ctx.setName(VK_OBJECT_TYPE_SAMPLER, m_thumbnailSampler, "editor thumbnails");

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
    initInfo.PipelineCache = ctx.pipelineCache();
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
    retireThumbnails();
    collectThumbnails(true);
    vkDestroySampler(m_app->context().device(), m_thumbnailSampler, nullptr);
    m_thumbnailSampler = VK_NULL_HANDLE;

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
        m_app->requestLoad(*pending);
    }
    collectThumbnails(false);

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
    if (ImGui::BeginTabBar("SceneTabs")) {
        if (ImGui::BeginTabItem("Scene")) {
            drawSceneTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Materials")) {
            drawMaterialsTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void Editor::drawSceneTab()
{

    const std::filesystem::path &currentModel = m_app->currentModel();
    if (m_app->isLoading()) {
        ImGui::TextUnformatted("Loading...");
    } else if (currentModel.empty()) {
        ImGui::TextUnformatted("No model loaded");
    } else {
        ImGui::TextUnformatted(currentModel.filename().string().c_str());
        ImGui::SetItemTooltip("%s", currentModel.string().c_str());
    }
    if (ImGui::Button("Open glTF...")) {
        showOpenDialog();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("or drop a file on the window");

    ImGui::SeparatorText("Model");
    ImGui::Text("%zu meshes, %u materials", m_app->geometry().liveMeshCount(), m_app->resources().materials().used());

    scene::Scene &scene = m_app->scene();
    if (const scene::NodeHandle root = m_app->modelRoot(); scene.alive(root)) {
        scene::Transform local = scene.local(root);
        bool changed = ImGui::DragFloat3("Root position", &local.translation.x, 0.05f);
        float scale = local.scale.x;
        if (ImGui::DragFloat("Root scale", &scale, 0.001f, 0.0001f, 1000.0f, "%.4f", ImGuiSliderFlags_Logarithmic)) {
            local.scale = glm::vec3(scale);
            changed = true;
        }
        if (changed) {
            scene.setLocal(root, local);
        }
    }
    ImGui::Text("%u of %u nodes used", scene.indexCount(), scene.capacity());
    ImGui::Text("%u mesh renderers, %u lights, %u cameras", scene.components<scene::MeshRenderer>().size(),
                scene.components<scene::Light>().size(), scene.components<scene::CameraComponent>().size());

    ImGui::SeparatorText("Camera");
    Camera &camera = m_app->camera();
    ImGui::SliderFloat("FOV", &camera.fovDegrees, 20.0f, 120.0f, "%.0f deg");
    ImGui::DragFloat("Near", &camera.nearPlane, 0.001f, 0.0001f, camera.infiniteFar ? 100.0f : camera.farPlane, "%.4f");
    ImGui::Checkbox("Infinite far", &camera.infiniteFar);
    ImGui::BeginDisabled(camera.infiniteFar);
    ImGui::DragFloat("Far", &camera.farPlane, 1.0f, camera.nearPlane, 100000.0f, "%.0f");
    ImGui::EndDisabled();
    ImGui::DragFloat("Speed", &camera.speed, 0.1f, 0.01f, 1000.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    if (ImGui::Button("Look at origin")) {
        camera.lookAt(glm::vec3(0.0f));
    }
    ImGui::SameLine();
    if (ImGui::Button("Frame model")) {
        frameModel();
    }
    ImGui::TextDisabled("Hold RMB: look, WASD/QE fly. MMB drag: pan");

    ImGui::SeparatorText("Frame");
    const FrameStats &stats = m_app->frameStats();
    const float average = stats.average();
    ImGui::Text("%.2f ms avg, %.2f ms max (%.0f FPS)", average, stats.maximum(), average > 0.0f ? 1000.0f / average : 0.0f);
    ImGui::PlotLines("##frametimes", stats.data(), static_cast<int>(stats.count()), static_cast<int>(stats.oldest()),
                     nullptr, 0.0f, std::max(stats.maximum(), 1.0f) * 1.2f, ImVec2(-1.0f, 60.0f));

    const render::SceneExtract::Stats instances = m_app->renderer().extract().stats();
    ImGui::Text("Instances: %u written, %u copies, %llu live", instances.written, instances.copyRegions,
                static_cast<unsigned long long>(instances.live));

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
}

void Editor::drawMaterialsTab()
{
    ResourceStore         &resources = m_app->resources();
    render::MaterialTable &materials = resources.materials();

    if (m_thumbnailEpoch != m_app->scene().epoch()) {
        m_thumbnailEpoch = m_app->scene().epoch();
        retireThumbnails();
        m_selectedMaterial = 0;
    }

    m_materialSlots.assign(1, 0);
    for (const core::Range &range : materials.ranges()) {
        for (uint64_t i = 0; i < range.count; ++i) {
            m_materialSlots.push_back(static_cast<uint32_t>(range.offset + i));
        }
    }
    if (std::find(m_materialSlots.begin(), m_materialSlots.end(), m_selectedMaterial) == m_materialSlots.end()) {
        m_selectedMaterial = 0;
    }

    ImGui::Text("%zu materials", m_materialSlots.size());

    const ImGuiStyle &style = ImGui::GetStyle();
    constexpr float ThumbnailSize = 64.0f;
    const ImVec2 imageSize(ThumbnailSize, ThumbnailSize);
    const ImVec2 cellSize(ThumbnailSize + 2.0f * style.FramePadding.x, ThumbnailSize + 2.0f * style.FramePadding.y);

    if (ImGui::BeginChild("MaterialGrid", ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.55f), ImGuiChildFlags_Borders)) {
        const float available = ImGui::GetContentRegionAvail().x;
        const int columns = std::max(1, static_cast<int>((available + style.ItemSpacing.x) / (cellSize.x + style.ItemSpacing.x)));

        for (size_t n = 0; n < m_materialSlots.size(); ++n) {
            const uint32_t  slot     = m_materialSlots[n];
            const Material &material = materials.read(slot);
            const ImVec4    tint(material.baseColor.r, material.baseColor.g, material.baseColor.b, material.baseColor.a);

            if (n % static_cast<size_t>(columns) != 0) {
                ImGui::SameLine();
            }
            ImGui::PushID(static_cast<int>(slot));
            bool clicked = false;
            if (const gfx::Image *image = resources.textureImage(material.textureIndex)) {
                clicked = ImGui::ImageButton("thumbnail", (ImTextureID)thumbnail(image->view), imageSize,
                                             ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0.05f, 0.05f, 0.05f, 1.0f), tint);
            } else {
                clicked = ImGui::ColorButton("thumbnail", tint, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreviewHalf, cellSize);
            }
            if (slot == m_selectedMaterial) {
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                    ImGui::GetColorU32(ImGuiCol_CheckMark), style.FrameRounding, 0, 2.0f);
            }
            if (clicked) {
                m_selectedMaterial = slot;
            }
            const std::string_view name = materials.name(slot);
            ImGui::SetItemTooltip("%u: %.*s", slot, static_cast<int>(name.size()), name.data());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    const std::string_view name = materials.name(m_selectedMaterial);
    ImGui::SeparatorText("Selected material");
    ImGui::Text("Slot %u: %.*s", m_selectedMaterial, static_cast<int>(name.size()), name.data());

    Material material = materials.read(m_selectedMaterial);
    if (const gfx::Image *image = resources.textureImage(material.textureIndex)) {
        const ImVec4 tint(material.baseColor.r, material.baseColor.g, material.baseColor.b, material.baseColor.a);
        ImGui::ImageWithBg((ImTextureID)thumbnail(image->view), ImVec2(128.0f, 128.0f), ImVec2(0.0f, 0.0f),
                           ImVec2(1.0f, 1.0f), ImVec4(0.05f, 0.05f, 0.05f, 1.0f), tint);
        ImGui::SameLine();
        ImGui::Text("Base color texture\n%u x %u, %u mips", image->extent.width, image->extent.height, image->mipLevels);
    } else {
        ImGui::TextDisabled("No base color texture");
    }

    if (ImGui::ColorEdit4("Base color", &material.baseColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaPreviewHalf)) {
        materials.write(m_selectedMaterial, material);
    }
}

VkDescriptorSet Editor::thumbnail(VkImageView view)
{
    const auto it = m_thumbnails.find(view);
    if (it != m_thumbnails.end()) {
        return it->second;
    }
    const VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(m_thumbnailSampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_thumbnails.emplace(view, set);
    return set;
}

void Editor::retireThumbnails()
{
    const uint64_t safeAfter = m_app->context().queue().lastSubmitted() + 1;
    for (const auto &[view, set] : m_thumbnails) {
        m_retiredThumbnails.push_back({ set, safeAfter });
    }
    m_thumbnails.clear();
}

void Editor::collectThumbnails(bool all)
{
    if (m_retiredThumbnails.empty()) {
        return;
    }
    const uint64_t completed = all ? UINT64_MAX : m_app->context().queue().completed();
    std::erase_if(m_retiredThumbnails, [&](const RetiredThumbnail &retired) {
        if (retired.safeAfter > completed) {
            return false;
        }
        ImGui_ImplVulkan_RemoveTexture(retired.set);
        return true;
    });
}

void Editor::frameModel()
{
    const scene::Scene &scene = m_app->scene();
    const scene::ComponentTable<scene::MeshRenderer> &renderers = scene.components<scene::MeshRenderer>();
    const std::span<const glm::mat4> worlds = scene.worlds();

    glm::vec3 lo(FLT_MAX);
    glm::vec3 hi(-FLT_MAX);
    bool found = false;
    for (uint32_t slot = 0; slot < renderers.size(); ++slot) {
        const Mesh *mesh = m_app->geometry().get(renderers.values()[slot].mesh);
        if (!mesh) {
            continue;
        }
        const glm::mat4 &world = worlds[renderers.nodes()[slot]];
        const float maxScale = std::max({ glm::length(glm::vec3(world[0])),
                                          glm::length(glm::vec3(world[1])),
                                          glm::length(glm::vec3(world[2])) });
        for (const SubMesh &subMesh : mesh->subMeshes) {
            const glm::vec3 centre = glm::vec3(world * glm::vec4(glm::vec3(subMesh.sphere), 1.0f));
            const float     radius = subMesh.sphere.w * maxScale;
            lo = glm::min(lo, centre - radius);
            hi = glm::max(hi, centre + radius);
            found = true;
        }
    }
    if (!found) {
        return;
    }

    Camera &camera = m_app->camera();
    const VkExtent2D extent = m_app->swapchain().extent();
    const float aspect = extent.height ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;
    const float halfVertical   = glm::radians(camera.fovDegrees) * 0.5f;
    const float halfHorizontal = std::atan(std::tan(halfVertical) * aspect);
    const float halfFov        = std::min(halfVertical, halfHorizontal);

    const glm::vec3 centre   = (lo + hi) * 0.5f;
    const float     radius   = glm::length(hi - lo) * 0.5f;
    const float     distance = radius / std::sin(halfFov);
    const glm::vec3 forward  = glm::vec3(camera.rotation() * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));

    camera.position = centre - forward * distance;
    camera.farPlane = std::max(camera.farPlane, distance + radius);
}
