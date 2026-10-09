#include "Application.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>

#include "assets/GltfLoader.h"
#include "core/Log.h"

namespace
{

// RENDERER_SHADER_DIR overrides <bin>/shaders: debug run can read the source tree.
std::filesystem::path shaderDirectory(const std::filesystem::path &base)
{
    const char *overrideDir = SDL_getenv("RENDERER_SHADER_DIR");
    if (overrideDir && *overrideDir) {
        std::error_code ec;
        const std::filesystem::path dir = std::filesystem::absolute(overrideDir, ec);
        if (ec || !std::filesystem::is_directory(dir, ec)) {
            core::fatal(std::format("RENDERER_SHADER_DIR is not a directory: {}", overrideDir));
        }
        core::log(std::format("Shaders from RENDERER_SHADER_DIR: {}", dir.string()));
        return dir;
    }
#ifndef NDEBUG
    core::log("Shaders from <bin>/shaders; set RENDERER_SHADER_DIR=<repo>/shaders to hot-reload source edits");
#endif
    return base / "shaders";
}

}

void Application::init()
{
    m_initStartNs = SDL_GetTicksNS();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        core::fatal(std::format("SDL_Init failed: {}", SDL_GetError()));
    }

    m_window = SDL_CreateWindow("Vulkan Renderer", 1280, 720,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!m_window) {
        core::fatal(std::format("SDL_CreateWindow failed: {}", SDL_GetError()));
    }

    const std::filesystem::path base = SDL_GetBasePath();
    const std::filesystem::path cacheDir = base / "cache";

    m_ctx.init(m_window, cacheDir);
    m_uploader.init(m_ctx,64ull << 20);
    m_swapchain.create(m_window);

    m_resources.initialize();
    m_renderer.init(shaderDirectory(base), cacheDir);

    m_scene.initialize(MaxNodes);
    m_geometry.init();
}

bool Application::loadData(const std::filesystem::path &modelPath)
{
    m_scene.clear();
    m_modelRoot = {};
    m_geometry.clear();
    m_resources.clearModelData();

    GltfLoader loader(m_ctx, m_resources, m_geometry, m_scene);
    if (!loader.load(modelPath)) {
        core::warn("Failed to load model: " + modelPath.string());
        return false;
    }
    m_modelRoot = loader.root();


    m_resources.updateTextureDescriptors();
    m_resources.uploadMaterialBuffer();

    m_uploader.flush();
    core::log(std::format("Loaded {} meshes, {} materials", m_geometry.liveMeshCount(), m_resources.materialCount()));
    const core::RangeAllocator &vertices = m_geometry.vertexAllocator();
    const core::RangeAllocator &indices  = m_geometry.indexAllocator();
    core::log(std::format("Geometry: {} of {} vertices used (largest free {}), {} of {} indices used (largest free {})",
                          vertices.used(), vertices.capacity(), vertices.largestFree(),
                          indices.used(), indices.capacity(), indices.largestFree()));
    return true;
}

void Application::run()
{
    m_running = true;
    const bool *keys = SDL_GetKeyboardState(nullptr);
    uint64_t prevTime = SDL_GetTicksNS();

    while (m_running) {
        SDL_Event event;
        if (m_minimized && SDL_WaitEvent(&event)) {   // sleep instead of spinning
            handleEvent(event);
        }
        while (SDL_PollEvent(&event)) {
            handleEvent(event);
        }
        if (!m_running || m_minimized) {
            prevTime = SDL_GetTicksNS();
            continue;
        }

        const uint64_t now = SDL_GetTicksNS();
        const float deltaTime = static_cast<float>(now - prevTime) * 1e-9f;
        prevTime = now;
        m_frameStats.add(deltaTime * 1000.0f);
        const auto up = m_uploader.takeStats();
        m_frameStats.uploadBytes = up.bytes;
        m_frameStats.uploadStalls = up.stalls;
        m_frameStats.uploadStallNs = up.stallNs;

        if (m_layer) {
            m_layer->onUpdate(deltaTime);
        }
        if (!m_layer || !m_layer->wantsKeyboard()) {
            m_camera.update(keys, deltaTime);
        }
        m_scene.update();
        m_renderer.render(m_scene, m_camera);

        if (!m_firstFrameLogged && m_renderer.frameNumber() > 0) {
            m_firstFrameLogged = true;
            core::log(std::format("First frame after {:.1f} ms", static_cast<double>(SDL_GetTicksNS() - m_initStartNs) * 1e-6));
        }
    }
}

void Application::handleEvent(const SDL_Event &event)
{
    if (m_layer && m_layer->onEvent(event)) {
        return;
    }
    m_camera.handleEvent(event);

    switch (event.type) {
    case SDL_EVENT_QUIT:
        m_running = false;
        break;
    case SDL_EVENT_KEY_DOWN:
        if (event.key.key == SDLK_ESCAPE) {
            m_running = false;
        } else if (event.key.key == SDLK_V && !event.key.repeat) {
            cyclePresentMode();
        } else if (event.key.key == SDLK_F5 && !event.key.repeat) {
            m_renderer.reloadShaders();
        }
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        m_swapchain.flagForRecreate();
        break;
    case SDL_EVENT_WINDOW_MINIMIZED:
        m_minimized = true;
        break;
    case SDL_EVENT_WINDOW_RESTORED:
        m_minimized = false;
        m_swapchain.flagForRecreate();
        break;
    default:
        break;
    }
}

void Application::cyclePresentMode()
{
    const std::vector<VkPresentModeKHR> &modes = m_swapchain.supportedPresentModes();
    if (modes.empty()) {
        return;
    }
    auto it = std::ranges::find(modes, m_swapchain.presentMode());
    it = (it == modes.end() || std::next(it) == modes.end()) ? modes.begin() : std::next(it);

    m_swapchain.setPresentMode(*it);
    core::log(std::format("Present mode: {}", gfx::presentModeName(*it)));
}

void Application::shutdown()
{
    m_uploader.flush();
    if (m_ctx.device()) {
        vkDeviceWaitIdle(m_ctx.device());
    }
    m_renderer.shutdown();
    m_geometry.shutdown();
    m_resources.shutdown();
    m_swapchain.destroy();

    m_uploader.destroy();
    m_ctx.shutdown();

    SDL_DestroyWindow(m_window);
    SDL_Quit();
}
