#include "ResourceStore.h"
#include "gfx/Context.h"
#include "gfx/StagingUploader.h"
#include <array>

namespace
{

    bool sameSampler(const VkSamplerCreateInfo &a, const VkSamplerCreateInfo &b)
    {
        return a.flags == b.flags &&
               a.magFilter == b.magFilter &&
               a.minFilter == b.minFilter &&
               a.mipmapMode == b.mipmapMode &&
               a.addressModeU == b.addressModeU &&
               a.addressModeV == b.addressModeV &&
               a.addressModeW == b.addressModeW &&
               a.mipLodBias == b.mipLodBias &&
               a.anisotropyEnable == b.anisotropyEnable &&
               a.maxAnisotropy == b.maxAnisotropy &&
               a.compareEnable == b.compareEnable &&
               a.compareOp == b.compareOp &&
               a.minLod == b.minLod &&
               a.maxLod == b.maxLod &&
               a.borderColor == b.borderColor &&
               a.unnormalizedCoordinates == b.unnormalizedCoordinates;
    }

}

void ResourceStore::initialize()
{
    createDescriptorSets();
    createFallbackTexture();

    addMaterial(Material
    {
        .baseColor = glm::vec4(1.0f),
        .textureIndex = textureDescriptorSlot(m_fallbackTextureId),
        .samplerIndex = samplerDescriptorSlot(m_fallbackTextureId)
    });

}

void ResourceStore::shutdown()
{
    if (!m_ctx.device()) {
        return;
    }

    if (m_globalLayout) {
        vkDestroyDescriptorSetLayout(m_ctx.device(), m_globalLayout, nullptr);
        m_globalLayout = VK_NULL_HANDLE;
    }
    if (m_descriptorPool) {
        // Frees m_globalDescSet implicitly.
        vkDestroyDescriptorPool(m_ctx.device(), m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
        m_globalDescSet = VK_NULL_HANDLE;
    }

    for (gfx::Image &img : m_images) {
        m_ctx.retire(img);
    }
    m_images.clear();

    for (VkSampler sampler : m_samplers) {
        m_ctx.retire([device = m_ctx.device(),sampler]
        {vkDestroySampler(device,sampler,nullptr);});
    }
    m_samplers.clear();
    m_samplerInfos.clear();

    for (gfx::Buffer &buff : m_buffers) {
        m_ctx.retire(buff);
    }
    m_buffers.clear();

    m_textures.clear();
    m_materials.clear();
    m_freeTextureIds.clear();
    m_pendingTextureWrites.clear();
}


void ResourceStore::clearModelData()
{
    for (size_t i = m_fallbackImageId; i < m_images.size(); ++i) {
        m_ctx.retire(m_images[i]);
    }
    m_images.resize(m_fallbackImageId);

    for (gfx::Buffer &buff : m_buffers) {
        m_ctx.retire(buff);
    }
    m_buffers.clear();
    m_materialBufferId = 0;

    std::vector<uint32_t> deadSlots;
    for (uint32_t id = m_fallbackTextureId + 1; id <= m_textures.size(); ++id) {
        Texture &texture = m_textures[id - 1];
        if (texture.imageId != 0) {
            texture = Texture{};
            deadSlots.push_back(id);
        }
    }
    std::erase_if(m_pendingTextureWrites, [this](uint32_t id) { return id != m_fallbackTextureId; });
    if (!deadSlots.empty()) {
        m_ctx.retire([this, slots = std::move(deadSlots)] { releaseTextureSlots(slots); });
    }

    m_materials.resize(1);
}

void ResourceStore::releaseTextureSlots(const std::vector<uint32_t> &ids)
{
    if (m_globalDescSet) {
        const VkDescriptorImageInfo fallback
        {
            .imageView = m_images[m_fallbackImageId - 1].view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
        std::vector<VkWriteDescriptorSet> writes;
        writes.reserve(ids.size());
        for (const uint32_t id : ids) {
            writes.push_back(VkWriteDescriptorSet
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = m_globalDescSet,
                .dstBinding = TextureBinding,
                .dstArrayElement = id - 1,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .pImageInfo = &fallback
            });
        }
        vkUpdateDescriptorSets(m_ctx.device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
    m_freeTextureIds.insert(m_freeTextureIds.end(), ids.begin(), ids.end());
}


uint32_t ResourceStore::addImage(const unsigned char *data, uint32_t width, uint32_t height, VkFormat format)
{
    const gfx::Image image = m_ctx.createImage({width, height}, format,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "Image");
    m_uploader.uploadImage(image, data, VkDeviceSize{width} * height * 4);

    m_images.push_back(image);
    return static_cast<uint32_t>(m_images.size());
}
uint32_t ResourceStore::addSampler(const VkSamplerCreateInfo &info)
{
    for (size_t i = 0; i < m_samplerInfos.size(); ++i) {
        if (sameSampler(m_samplerInfos[i], info)) {
            return static_cast<uint32_t>(i + 1);
        }
    }

    if (m_samplers.size() >= MaxSamplers) {
        core::warn("Exceeded the maximum sampler count");
        return m_fallbackSamplerId;
    }

    VkSampler sampler = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(m_ctx.device(), &info, nullptr, &sampler));
    m_samplers.push_back(sampler);
    m_samplerInfos.push_back(info);
    m_samplerInfos.back().pNext = nullptr;

    const uint32_t id = static_cast<uint32_t>(m_samplers.size());

    const VkDescriptorImageInfo samplerInfo{ .sampler = sampler };
    const VkWriteDescriptorSet write
    {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_globalDescSet,
        .dstBinding = SamplerBinding,
        .dstArrayElement = id - 1,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .pImageInfo = &samplerInfo
    };
    vkUpdateDescriptorSets(m_ctx.device(), 1, &write, 0, nullptr);

    return id;
}
uint32_t ResourceStore::addTexture(uint32_t imageId, uint32_t samplerId)
{
    uint32_t id = 0;
    if (!m_freeTextureIds.empty()) {
        id = m_freeTextureIds.back();
        m_freeTextureIds.pop_back();
    } else if (m_textures.size() >= MaxTextures) {
        core::warn("Exceeded the maximum texture count");
        return m_fallbackTextureId;
    } else {
        m_textures.emplace_back();
        id = static_cast<uint32_t>(m_textures.size());
    }

    if (imageId == 0 || imageId > m_images.size()) {
        imageId = m_fallbackImageId;
    }
    if (samplerId == 0 || samplerId > m_samplers.size()) {
        samplerId = m_fallbackSamplerId;
    }

    m_textures[id - 1] = Texture{ .imageId = imageId, .samplerId = samplerId };
    m_pendingTextureWrites.push_back(id);
    return id;
}

uint32_t ResourceStore::addMaterial(const Material &material)
{
    m_materials.push_back(material);
    return static_cast<uint32_t>(m_materials.size());
}

uint32_t ResourceStore::addBuffer(const gfx::Buffer &buffer)
{
    m_buffers.push_back(buffer);
    return static_cast<uint32_t>(m_buffers.size());
}

uint32_t ResourceStore::textureDescriptorSlot(uint32_t textureId) const
{
    // Descriptor array is 0-based; our IDs are 1-based. This is the ONLY
    // place that conversion is allowed to happen.
    if (textureId == 0 || textureId > m_textures.size()) {
        return m_fallbackTextureId ? m_fallbackTextureId - 1 : 0;
    }
    return textureId - 1;
}

uint32_t ResourceStore::samplerDescriptorSlot(uint32_t textureId) const
{
    const uint32_t fallbackSlot = m_fallbackSamplerId ? m_fallbackSamplerId - 1 : 0;
    if (textureId == 0 || textureId > m_textures.size()) {
        return fallbackSlot;
    }
    const uint32_t samplerId = m_textures[textureId - 1].samplerId;
    if (samplerId == 0 || samplerId > m_samplers.size()) {
        return fallbackSlot;
    }
    return samplerId - 1;
}

void ResourceStore::createFallbackTexture()
{
    // Magenta 1x1. Must be the first image, sampler and texture created, so
    // it always occupies descriptor slot 0.
    const uint32_t purplePixelData = 0xFFFF00FF;
    m_fallbackImageId = addImage(reinterpret_cast<const unsigned char *>(&purplePixelData), 1, 1, VK_FORMAT_R8G8B8A8_SRGB);

    const VkSamplerCreateInfo samplerInfo
    {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .compareEnable = VK_FALSE
    };

    m_fallbackSamplerId = addSampler(samplerInfo);
    m_fallbackTextureId = addTexture(m_fallbackImageId, m_fallbackSamplerId);
}


void ResourceStore::createDescriptorSets()
{
    const std::array<VkDescriptorPoolSize, 3> poolSizes
    {
        VkDescriptorPoolSize{ .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = MaxTextures },
        VkDescriptorPoolSize{ .type = VK_DESCRIPTOR_TYPE_SAMPLER,       .descriptorCount = MaxSamplers },
        VkDescriptorPoolSize{ .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = MaxStorageImages }
    };

    const VkDescriptorPoolCreateInfo poolInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()
    };
    VK_CHECK(vkCreateDescriptorPool(m_ctx.device(), &poolInfo, nullptr, &m_descriptorPool));

    const std::array<VkDescriptorSetLayoutBinding, 3> bindings
    {
        VkDescriptorSetLayoutBinding
        {
            .binding = TextureBinding,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = MaxTextures,
            .stageFlags = VK_SHADER_STAGE_ALL
        },
        VkDescriptorSetLayoutBinding
        {
            .binding = SamplerBinding,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = MaxSamplers,
            .stageFlags = VK_SHADER_STAGE_ALL
        },
        VkDescriptorSetLayoutBinding
        {
            .binding = StorageImageBinding,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = MaxStorageImages,
            .stageFlags = VK_SHADER_STAGE_ALL
        }
    };

    constexpr VkDescriptorBindingFlags bindingFlags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    const std::array<VkDescriptorBindingFlags, 3> flags{ bindingFlags, bindingFlags, bindingFlags };

    const VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(flags.size()),
        .pBindingFlags = flags.data()
    };

    const VkDescriptorSetLayoutCreateInfo layoutInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &flagsInfo,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data()
    };
    VK_CHECK(vkCreateDescriptorSetLayout(m_ctx.device(), &layoutInfo, nullptr, &m_globalLayout));

    const VkDescriptorSetAllocateInfo descSetAllocInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &m_globalLayout
    };
    VK_CHECK(vkAllocateDescriptorSets(m_ctx.device(), &descSetAllocInfo, &m_globalDescSet));
}

void ResourceStore::updateTextureDescriptors()
{
    if (m_pendingTextureWrites.empty()) {
        return;
    }

    std::vector<VkDescriptorImageInfo> imageInfos;
    imageInfos.reserve(m_pendingTextureWrites.size());
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(m_pendingTextureWrites.size());

    for (const uint32_t id : m_pendingTextureWrites) {
        const Texture &t = m_textures[id - 1];

        const bool imageOk = t.imageId > 0 && t.imageId <= m_images.size();
        if (!imageOk) {
            core::warn("Texture references an invalid image; using the fallback");
        }
        const gfx::Image &image = imageOk
            ? m_images[t.imageId - 1]
            : m_images[m_fallbackImageId - 1];

        imageInfos.push_back(VkDescriptorImageInfo
        {
            .imageView = image.view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        });
        writes.push_back(VkWriteDescriptorSet
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_globalDescSet,
            .dstBinding = TextureBinding,
            .dstArrayElement = id - 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &imageInfos.back()
        });
    }

    vkUpdateDescriptorSets(m_ctx.device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    m_pendingTextureWrites.clear();
}

void ResourceStore::uploadMaterialBuffer()
{
    if (m_materials.empty()) {
        return;
    }

    const size_t matDataBytes = m_materials.size() * sizeof(Material);

    gfx::Buffer matBuffer = m_ctx.createBuffer(matDataBytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, gfx::MemoryIntent::GpuOnly, "materials");
    m_uploader.uploadBuffer(matBuffer, 0, m_materials.data(), matDataBytes);
    m_materialBufferId = addBuffer(matBuffer);
}

uint64_t ResourceStore::materialBufferAddress() const
{
    if (!m_materialBufferId) {
        return 0;
    }
    return m_buffers[m_materialBufferId - 1].address;
}
