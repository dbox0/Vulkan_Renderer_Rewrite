#pragma once
#include <cstdint>
#include <vector>

#include "Types.h"
#include "gfx/Resources.h"

namespace gfx { class Context; class StagingUploader; }

// Owns every bindless GPU resource: images, samplers, textures, materials and
// raw buffers, plus the global descriptor set the fragment shader samples
// through.
//
// ID CONVENTION
//   * Returned IDs are 1-based:  id == vector index + 1.  id 0 == "none".
//   * Texture slot 0 (i.e. texture ID 1) is always the purple fallback
//   * Material::textureIndex is 0-BASED, because the shader indexes the
//     descriptor array directly. Converting is this class's job

class ResourceStore
{
public:
    static constexpr uint32_t MaxTextures      = 1024;
    static constexpr uint32_t MaxSamplers      = 32;
    static constexpr uint32_t MaxStorageImages = 64;

    static constexpr uint32_t TextureBinding      = 0;
    static constexpr uint32_t SamplerBinding      = 1;
    static constexpr uint32_t StorageImageBinding = 2;

    explicit ResourceStore(gfx::Context &ctx , gfx::StagingUploader &uploader) : m_ctx(ctx) , m_uploader(uploader) {}
    ResourceStore(const ResourceStore &) = delete;
    ResourceStore &operator=(const ResourceStore &) = delete;

    void initialize();          // descriptor pool/layout/set + purple fallback
    void shutdown();
    void clearModelData();      // keeps the fallbacks and default material


    uint32_t addImage(const unsigned char *data, uint32_t width, uint32_t height, VkFormat format);
    uint32_t addSampler(const VkSamplerCreateInfo &info);
    uint32_t addTexture(uint32_t imageId, uint32_t samplerId);
    uint32_t addMaterial(const Material &material);
    uint32_t addBuffer(const gfx::Buffer &buffer);

    uint32_t fallbackImageId()   const { return m_fallbackImageId; }
    uint32_t fallbackSamplerId() const { return m_fallbackSamplerId; }
    uint32_t fallbackTextureId() const { return m_fallbackTextureId; }

    // Converts a 1-based texture ID to the 0-based descriptor array slot the
    // shader uses. Returns the fallback slot for id 0 or out-of-range.
    uint32_t textureDescriptorSlot(uint32_t textureId) const;
    uint32_t samplerDescriptorSlot(uint32_t textureId) const;

    const gfx::Buffer &buffer(uint32_t bufferId) const { return m_buffers[bufferId - 1]; }
    size_t materialCount() const { return m_materials.size(); }
    const std::vector<Material> &materials() const { return m_materials; }

    void updateTextureDescriptors();       // writes the textures added since the last call into binding 0
    VkDescriptorSet       globalDescriptorSet() const { return m_globalDescSet; }
    VkDescriptorSetLayout globalLayout()        const { return m_globalLayout; }

    // Uploads m_materials into a buffer. called after all glTF loading.
    void uploadMaterialBuffer();
    uint64_t materialBufferAddress() const;

private:
    void createDescriptorSets();
    void createFallbackTexture();
    void releaseTextureSlots(const std::vector<uint32_t> &ids);

    gfx::Context &m_ctx;
    gfx::StagingUploader &m_uploader;

    std::vector<gfx::Image>  m_images;
    std::vector<VkSampler>   m_samplers;
    std::vector<VkSamplerCreateInfo> m_samplerInfos;
    std::vector<Texture>     m_textures;
    std::vector<Material>    m_materials;
    std::vector<gfx::Buffer> m_buffers;
    std::vector<uint32_t>    m_freeTextureIds;
    std::vector<uint32_t>    m_pendingTextureWrites;

    uint32_t m_fallbackImageId   = 0;
    uint32_t m_fallbackSamplerId = 0;
    uint32_t m_fallbackTextureId = 0;
    uint32_t m_materialBufferId  = 0;

    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_globalLayout   = VK_NULL_HANDLE;
    VkDescriptorSet       m_globalDescSet  = VK_NULL_HANDLE;
};
