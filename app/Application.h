#pragma once
#include <cstdint>
#include <filesystem>

#include <SDL3/SDL_events.h>

#include "AppLayer.h"
#include "core/FrameStats.h"
#include "gfx/Context.h"
#include "gfx/StagingUploader.h"
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


    void setLayer(AppLayer *layer) { m_layer = layer; }

    SDL_Window       *window()          { return m_window; }
    gfx::Context     &context()         { return m_ctx; }
    gfx::Swapchain   &swapchain()       { return m_swapchain; }
    render::Renderer &renderer()        { return m_renderer; }
    Scene            &scene()           { return m_scene; }
    Camera           &camera()          { return m_camera; }
    GeometryStore    &geometry()        { return m_geometry; }
    ResourceStore    &resources()       { return m_resources; }
    const core::FrameStats &frameStats() const { return m_frameStats; }
    void              quit()            { m_running = false; }

private:
    void handleEvent(const SDL_Event &event);
    void cyclePresentMode();

    static constexpr size_t   MaxNodes          = 4096;
    static constexpr size_t   MaxVertices       = 2ull * 1024 * 1024;
    static constexpr size_t   MaxIndices        = 8ull * 1024 * 1024;

    SDL_Window *m_window    = nullptr;
    bool        m_running   = false;
    bool        m_minimized = false;
    AppLayer   *m_layer     = nullptr;

    uint64_t m_initStartNs      = 0;
    bool     m_firstFrameLogged = false;

    core::FrameStats m_frameStats;

    // Declaration order is construction order; shutdown() tears down explicitly in reverse.
    gfx::Context     m_ctx;
    gfx::StagingUploader m_uploader;
    gfx::Swapchain   m_swapchain{ m_ctx };
    ResourceStore        m_resources{ m_ctx, m_uploader };
    GeometryStore        m_geometry{ m_ctx, m_uploader };


    Scene            m_scene;
    Camera           m_camera;
    render::Renderer m_renderer{ m_ctx, m_swapchain, m_uploader, m_resources, m_geometry };
};
