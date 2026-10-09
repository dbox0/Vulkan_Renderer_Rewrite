#include "Instantiate.h"

#include <format>
#include <vector>

#include "GeometryStore.h"
#include "ResourceStore.h"
#include "core/Log.h"
#include "scene/Scene.h"

namespace render
{

namespace
{

VkFilter toVk(assets::Filter filter)
{
    return filter == assets::Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

VkSamplerMipmapMode toVkMip(assets::Filter filter)
{
    return filter == assets::Filter::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

VkSamplerAddressMode toVk(assets::Wrap wrap)
{
    switch (wrap) {
    case assets::Wrap::ClampToEdge:    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case assets::Wrap::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    default:                           return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

VkSamplerCreateInfo toVk(const assets::ImportedSampler &sampler)
{
    return VkSamplerCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = toVk(sampler.magFilter),
        .minFilter = toVk(sampler.minFilter),
        .mipmapMode = toVkMip(sampler.mipFilter),
        .addressModeU = toVk(sampler.wrapU),
        .addressModeV = toVk(sampler.wrapV),
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .compareEnable = VK_FALSE,
        .minLod = 0.0f,
        .maxLod = sampler.useMips ? VK_LOD_CLAMP_NONE : 0.25f
    };
}

template <typename T>
bool inRange(int32_t index, const std::vector<T> &items)
{
    return index >= 0 && static_cast<size_t>(index) < items.size();
}

scene::Light toComponent(const assets::ImportedLight &light)
{
    return scene::Light
    {
        .type = light.type == assets::LightType::Directional ? scene::Light::Type::Directional
              : light.type == assets::LightType::Spot        ? scene::Light::Type::Spot
                                                              : scene::Light::Type::Point,
        .color = light.color,
        .intensity = light.intensity,
        .range = light.range,
        .innerCone = light.innerCone,
        .outerCone = light.outerCone
    };
}

}

scene::NodeHandle instantiate(const assets::ImportedScene &imported, std::string_view rootName,
                              scene::Scene &scene, ResourceStore &resources, GeometryStore &geometry)
{
    const uint64_t neededNodes = imported.nodes.size() + 1;
    if (neededNodes > uint64_t{ scene.capacity() } - scene.indexCount()) {
        core::warn(std::format("'{}' needs {} nodes but the scene has room for {}; not instantiated",
                               rootName, neededNodes, scene.capacity() - scene.indexCount()));
        return {};
    }

    std::vector<uint32_t> imageIds;
    imageIds.reserve(imported.images.size());
    std::vector<gfx::ImageLevel> levels;
    for (const assets::ImportedImage &image : imported.images) {
        if (image.mips.empty()) {
            imageIds.push_back(resources.fallbackImageId());
            continue;
        }
        levels.clear();
        for (const assets::MipLevel &mip : image.mips) {
            levels.push_back(gfx::ImageLevel{ .data = mip.rgba.data(), .size = mip.rgba.size(),
                                              .extent = { mip.width, mip.height } });
        }
        const VkFormat format = image.colorSpace == assets::ColorSpace::Srgb ? VK_FORMAT_R8G8B8A8_SRGB
                                                                             : VK_FORMAT_R8G8B8A8_UNORM;
        imageIds.push_back(resources.addImage(levels, format));
    }

    std::vector<uint32_t> samplerIds;
    samplerIds.reserve(imported.samplers.size());
    for (const assets::ImportedSampler &sampler : imported.samplers) {
        const uint32_t id = resources.addSampler(toVk(sampler));
        samplerIds.push_back(id ? id : resources.fallbackSamplerId());
    }

    std::vector<uint32_t> textureIds;
    textureIds.reserve(imported.textures.size());
    for (const assets::ImportedTexture &texture : imported.textures) {
        const uint32_t imageId = inRange(texture.image, imageIds) ? imageIds[static_cast<size_t>(texture.image)]
                                                                  : resources.fallbackImageId();
        const uint32_t samplerId = inRange(texture.sampler, samplerIds) ? samplerIds[static_cast<size_t>(texture.sampler)]
                                                                        : resources.fallbackSamplerId();
        textureIds.push_back(resources.addTexture(imageId, samplerId));
    }

    std::vector<uint32_t> materialSlots;
    materialSlots.reserve(imported.materials.size());
    for (const assets::ImportedMaterial &material : imported.materials) {
        const uint32_t textureId = inRange(material.baseColorTexture, textureIds)
            ? textureIds[static_cast<size_t>(material.baseColorTexture)]
            : resources.fallbackTextureId();
        const uint32_t materialId = resources.addMaterial(Material
        {
            .baseColor = material.baseColorFactor,
            .textureIndex = resources.textureDescriptorSlot(textureId),
            .samplerIndex = resources.samplerDescriptorSlot(textureId)
        });
        materialSlots.push_back(resources.materialShaderIndex(materialId));
    }

    std::vector<MeshHandle> meshes;
    meshes.reserve(imported.meshes.size());
    for (const assets::MeshData &mesh : imported.meshes) {
        meshes.push_back(mesh.subMeshes.empty() ? MeshHandle{} : geometry.addMesh(mesh, materialSlots));
    }

    const scene::NodeHandle root = scene.create(rootName);
    std::vector<scene::NodeHandle> nodes;
    nodes.reserve(imported.nodes.size());
    for (size_t i = 0; i < imported.nodes.size(); ++i) {
        const assets::ImportedNode &source = imported.nodes[i];
        if (source.parent >= static_cast<int32_t>(i)) {
            core::fatal(std::format("Imported node {} has parent {}; parents must come first", i, source.parent));
        }
        const scene::NodeHandle parent = source.parent < 0 ? root : nodes[static_cast<size_t>(source.parent)];
        const scene::NodeHandle node = parent.valid() ? scene.create(source.name, parent) : scene::NodeHandle{};
        nodes.push_back(node);
        if (!node.valid()) {
            continue;
        }

        scene.setLocal(node, source.local);
        if (inRange(source.mesh, meshes) && meshes[static_cast<size_t>(source.mesh)].valid()) {
            scene.setComponent(node, scene::MeshRenderer{ meshes[static_cast<size_t>(source.mesh)] });
        }
        if (inRange(source.light, imported.lights)) {
            scene.setComponent(node, toComponent(imported.lights[static_cast<size_t>(source.light)]));
        }
        if (inRange(source.camera, imported.cameras) && imported.cameras[static_cast<size_t>(source.camera)].perspective) {
            const assets::ImportedCamera &camera = imported.cameras[static_cast<size_t>(source.camera)];
            scene.setComponent(node, scene::CameraComponent{ .yFov = camera.yFov, .zNear = camera.zNear, .zFar = camera.zFar });
        }
    }
    return root;
}

}
