#pragma once
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

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
    void drawSceneTab();
    void drawMaterialsTab();
    void frameModel();

    VkDescriptorSet thumbnail(VkImageView view);
    void            retireThumbnails();
    void            collectThumbnails(bool all);

    Application     *m_app  = nullptr;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;

    std::mutex                           m_pendingMutex;   // the file dialog answers on its own thread
    std::optional<std::filesystem::path> m_pendingModel;

    struct RetiredThumbnail
    {
        VkDescriptorSet set;
        uint64_t        safeAfter;
    };
    VkSampler                                        m_thumbnailSampler = VK_NULL_HANDLE;
    std::unordered_map<VkImageView, VkDescriptorSet> m_thumbnails;
    std::vector<RetiredThumbnail>                    m_retiredThumbnails;
    uint64_t                                         m_thumbnailEpoch   = 0;
    uint32_t                                         m_selectedMaterial = 0;
    std::vector<uint32_t>                            m_materialSlots;
};
