#pragma once
#include <cstdint>
#include <filesystem>

#include <SDL3/SDL_events.h>

#include "gfx/Context.h"
#include "gfx/Swapchain.h"
#include "render/GeometryStore.h"
#include "render/Renderer.h"
#include "render/ResourceStore.h"
#include "scene/Camera.h"
#include "scene/Scene.h"

// Window, main loop and wiring.
class Application
{
public:
    void init();
    bool loadData(const std::filesystem::path &modelPath);
    void run();
    void shutdown();

private:
    void handleEvent(const SDL_Event &event);

    static constexpr size_t   MaxNodes          = 4096;
    static constexpr size_t   VertexBudgetBytes = 64ull * 1024 * 1024;
    static constexpr size_t   IndexBudgetBytes  = 32ull * 1024 * 1024;
    static constexpr uint32_t MaxDrawsPerFrame  = 8192;

    SDL_Window *m_window    = nullptr;
    bool        m_running   = false;
    bool        m_minimized = false;

    // Declaration order is construction order; shutdown() tears down explicitly in reverse.
    gfx::Context     m_ctx;
    gfx::Swapchain   m_swapchain{ m_ctx };
    ResourceStore    m_resources{ m_ctx };
    GeometryStore    m_geometry{ m_ctx };
    Scene            m_scene;
    Camera           m_camera;
    render::Renderer m_renderer{ m_ctx, m_swapchain, m_resources, m_geometry };
};
