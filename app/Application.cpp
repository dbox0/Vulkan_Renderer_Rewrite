#include "Application.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>

#include "assets/GltfLoader.h"
#include "core/Log.h"

void Application::init()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        core::fatal(std::format("SDL_Init failed: {}", SDL_GetError()));
    }

    m_window = SDL_CreateWindow("Vulkan Renderer", 1280, 720,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!m_window) {
        core::fatal(std::format("SDL_CreateWindow failed: {}", SDL_GetError()));
    }

    m_ctx.init(m_window);
    m_swapchain.create(m_window);

    // Must come before the renderer: the pipeline layout needs the global descriptor set layout.
    m_resources.initialize();
    m_renderer.init(std::filesystem::path(SDL_GetBasePath()) / "shaders", MaxDrawsPerFrame);

    m_scene.initialize(MaxNodes);
    m_geometry.reserve(VertexBudgetBytes, IndexBudgetBytes);
}

bool Application::loadData(const std::filesystem::path &modelPath)
{
    VK_CHECK(vkDeviceWaitIdle(m_ctx.device()));
    m_scene.clear();
    m_geometry.reset();
    m_resources.clearModelData();

    GltfLoader loader(m_ctx, m_resources, m_geometry, m_scene);
    if (!loader.load(modelPath)) {
        core::warn("Failed to load model: " + modelPath.string());
        return false;
    }


    // Each of these commits a snapshot of a store to the GPU, so they run after ALL loading.
    if (!m_geometry.uploadToGpu()) {
        return false;
    }
    m_resources.updateTextureDescriptors();
    m_resources.uploadMaterialBuffer();

    core::log(std::format("Loaded {} meshes, {} materials", m_geometry.meshCount(), m_resources.materialCount()));
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

        if (m_layer) {
            m_layer->onUpdate(deltaTime);
        }
        if (!m_layer || !m_layer->wantsKeyboard()) {
            m_camera.update(keys, deltaTime);
        }
        m_renderer.render(m_scene, m_camera);
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
    if (m_ctx.device()) {
        vkDeviceWaitIdle(m_ctx.device());
    }
    m_renderer.shutdown();
    m_geometry.shutdown();
    m_resources.shutdown();
    m_swapchain.destroy();
    m_ctx.shutdown();

    SDL_DestroyWindow(m_window);
    SDL_Quit();
}
