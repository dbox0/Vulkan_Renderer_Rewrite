#pragma once
#include <filesystem>
#include <mutex>
#include <optional>

#include "AppLayer.h"
#include "gfx/Vk.h"

class Application;

// ImGui editor. Attaches to an Application as a layer; the app and renderer run without it.
class Editor final : public AppLayer
{
public:
    void attach(Application &app);
    void detach();

    // Loads at the start of the next frame. Safe to call from any thread.
    void openModel(const std::filesystem::path &path);

    bool onEvent(const SDL_Event &event) override;
    bool wantsKeyboard() const override;
    void onUpdate(float deltaTime) override;

private:
    void showOpenDialog();
    void drawMenuBar();
    void drawScenePanel();
    void frameModel();

    Application     *m_app  = nullptr;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;

    std::mutex                           m_pendingMutex;   // the file dialog answers on its own thread
    std::optional<std::filesystem::path> m_pendingModel;
    std::filesystem::path                m_currentModel;
};
