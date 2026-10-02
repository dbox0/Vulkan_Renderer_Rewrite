#include "ResourceStore.h"
#include "gfx/Context.h"
#include "gfx/StagingUploader.h"
#include <array>

void ResourceStore::initialize()
{
    createDescriptorSets();
    createFallbackTexture();

    // Material index 0 is the default, used by primitives with no material.
    // Without it a model with no materials would read through a null material pointer.
    addMaterial(Material{ .textureIndex = textureDescriptorSlot(m_fallbackTextureId) });
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

    for (gfx::Buffer &buff : m_buffers) {
        m_ctx.retire(buff);
    }
    m_buffers.clear();

    m_textures.clear();
    m_materials.clear();
}


void ResourceStore::clearModelData()
{
    // The fallback image, sampler and texture are always first
    // default material too.
    for (size_t i = m_fallbackImageId; i < m_images.size(); ++i) {
        m_ctx.retire(m_images[i]);
    }
    m_images.resize(m_fallbackImageId);

    for (size_t i = m_fallbackSamplerId; i < m_samplers.size(); ++i) {
        m_ctx.retire([device = m_ctx.device(),sampler = m_samplers[i]]{
            vkDestroySampler(device, sampler, nullptr);
        });

    }
    m_samplers.resize(m_fallbackSamplerId);

    for (gfx::Buffer &buff : m_buffers) {
        m_ctx.retire(buff);
    }
    m_buffers.clear();
    m_materialBufferId = 0;

    m_textures.resize(m_fallbackTextureId);
    m_materials.resize(1);
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
    VkSampler sampler = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(m_ctx.device(), &info, nullptr, &sampler));
    m_samplers.push_back(sampler);
    return static_cast<uint32_t>(m_samplers.size());
}

uint32_t ResourceStore::addTexture(uint32_t imageId, uint32_t samplerId)
{
    if (m_textures.size() >= MaxTextures) {
        core::warn("Exceeded the maximum texture count");
        return m_fallbackTextureId;
    }

    if (imageId == 0 || imageId > m_images.size()) {
        imageId = m_fallbackImageId;
    }
    if (samplerId == 0 || samplerId > m_samplers.size()) {
        samplerId = m_fallbackSamplerId;
    }

    m_textures.push_back(Texture{ .imageId = imageId, .samplerId = samplerId });
    return static_cast<uint32_t>(m_textures.size());
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

// fallback texture

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

// bindless descriptors

void ResourceStore::createDescriptorSets()
{
    const std::array<VkDescriptorPoolSize, 1> poolSizes
    {
        VkDescriptorPoolSize
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = MaxTextures
        }
    };

    const VkDescriptorPoolCreateInfo poolInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        // Lets us rewrite descriptors after they have been bound.
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()
    };
    VK_CHECK(vkCreateDescriptorPool(m_ctx.device(), &poolInfo, nullptr, &m_descriptorPool));

    const std::array<VkDescriptorSetLayoutBinding, 1> bindings
    {
        VkDescriptorSetLayoutBinding
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = MaxTextures,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
        }
    };

    const std::array<VkDescriptorBindingFlags, 1> flags
    {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
    };

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
    if (m_textures.empty()) {
        return;
    }

    std::vector<VkDescriptorImageInfo> imageDescriptors;
    imageDescriptors.reserve(m_textures.size());

    for (const Texture &t : m_textures) {
        // addTexture already substitutes the fallback for invalid IDs, but
        // check again: a null sampler or imageView reaching this write is a
        // validation error

        const bool imageOk   = t.imageId   > 0 && t.imageId   <= m_images.size();
        const bool samplerOk = t.samplerId > 0 && t.samplerId <= m_samplers.size();

        if (!imageOk || !samplerOk) {
            core::warn("Texture references an invalid image or sampler; using the fallback");
        }

        const gfx::Image &image = imageOk
            ? m_images[t.imageId - 1]
            : m_images[m_fallbackImageId - 1];
        const VkSampler sampler = samplerOk
            ? m_samplers[t.samplerId - 1]
            : m_samplers[m_fallbackSamplerId - 1];

        imageDescriptors.push_back(
            {
                .sampler = sampler,
                .imageView = image.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            });
    }

    const VkWriteDescriptorSet writeDescriptorSet
    {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_globalDescSet,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = static_cast<uint32_t>(imageDescriptors.size()),
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = imageDescriptors.data()
    };

    vkUpdateDescriptorSets(m_ctx.device(), 1, &writeDescriptorSet, 0, nullptr);
}

// material buffer

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
