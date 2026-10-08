#include "GltfLoader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <iostream>
#include <numeric>
#include <tuple>
#include <unordered_map>

#include <glm/gtc/packing.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <tiny_gltf_v3.h>
#include <stb_image.h>

#include "gfx/Context.h"
#include "render/GeometryStore.h"
#include "render/ResourceStore.h"
#include "render/Types.h"
#include "scene/Scene.h"

namespace
{

const glm::vec3 DefaultNormal(0.0f, 1.0f, 0.0f);
constexpr uint32_t White = 0xFFFFFFFFu;

float readComponent(const unsigned char *src, int32_t componentType, bool normalized)
{
    switch (componentType) {
    case TG3_COMPONENT_TYPE_FLOAT: {
        float value = 0.0f;
        std::memcpy(&value, src, sizeof(value));
        return value;
    }
    case TG3_COMPONENT_TYPE_UNSIGNED_BYTE: {
        const float value = static_cast<float>(*src);
        return normalized ? value / 255.0f : value;
    }
    case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: {
        uint16_t raw = 0;
        std::memcpy(&raw, src, sizeof(raw));
        const float value = static_cast<float>(raw);
        return normalized ? value / 65535.0f : value;
    }
    case TG3_COMPONENT_TYPE_BYTE: {
        int8_t raw = 0;
        std::memcpy(&raw, src, sizeof(raw));
        const float value = static_cast<float>(raw);
        return normalized ? std::max(value / 127.0f, -1.0f) : value;
    }
    case TG3_COMPONENT_TYPE_SHORT: {
        int16_t raw = 0;
        std::memcpy(&raw, src, sizeof(raw));
        const float value = static_cast<float>(raw);
        return normalized ? std::max(value / 32767.0f, -1.0f) : value;
    }
    default:
        return 0.0f;
    }
}

glm::vec4 readElement(const tg3_model &model, const tg3_accessor &accessor, uint64_t index)
{
    glm::vec4 result(0.0f, 0.0f, 0.0f, 1.0f);
    if (accessor.buffer_view < 0) {
        return result;
    }

    const tg3_buffer_view &view   = model.buffer_views[accessor.buffer_view];
    const tg3_buffer      &buffer = model.buffers[view.buffer];

    const int32_t stride          = tg3_accessor_byte_stride(&accessor, &view);
    const int32_t componentSize   = tg3_component_size(accessor.component_type);
    const int32_t componentCount  = std::min(tg3_num_components(accessor.type), 4);
    if (stride <= 0 || componentSize <= 0 || componentCount <= 0) {
        return result;
    }

    const unsigned char *element = buffer.data.data + view.byte_offset + accessor.byte_offset
                                 + index * static_cast<uint64_t>(stride);
    for (int32_t c = 0; c < componentCount; ++c) {
        result[c] = readComponent(element + c * componentSize, accessor.component_type, accessor.normalized != 0);
    }
    return result;
}

const tg3_accessor *findAttribute(const tg3_model &model, const tg3_primitive &primitive, const char *name)
{
    for (uint32_t a = 0; a < primitive.attributes_count; ++a) {
        if (std::strcmp(primitive.attributes[a].key.data, name) == 0) {
            return &model.accessors[primitive.attributes[a].value];
        }
    }
    return nullptr;
}

bool readIndices(const tg3_model &model, const tg3_accessor &accessor, uint32_t *dst)
{
    if (accessor.buffer_view < 0) {
        return false;
    }
    const tg3_buffer_view &view = model.buffer_views[accessor.buffer_view];
    const unsigned char *src = model.buffers[view.buffer].data.data + view.byte_offset + accessor.byte_offset;

    switch (accessor.component_type) {
    case TG3_COMPONENT_TYPE_UNSIGNED_INT:
        std::memcpy(dst, src, accessor.count * sizeof(uint32_t));
        return true;
    case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
        for (uint64_t i = 0; i < accessor.count; ++i) {
            uint16_t value = 0;
            std::memcpy(&value, src + i * sizeof(uint16_t), sizeof(value));
            dst[i] = value;
        }
        return true;
    case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
        for (uint64_t i = 0; i < accessor.count; ++i) {
            dst[i] = src[i];
        }
        return true;
    default:
        return false;
    }
}

glm::vec2 octEncode(glm::vec3 n)
{
    n /= std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
    glm::vec2 p(n.x, n.y);
    if (n.z < 0.0f) {
        const glm::vec2 s(p.x >= 0.0f ? 1.0f : -1.0f, p.y >= 0.0f ? 1.0f : -1.0f);
        p = (1.0f - glm::abs(glm::vec2(p.y, p.x))) * s;
    }
    return p;
}

uint32_t encodeNormal(glm::vec3 n)
{
    if (glm::dot(n, n) < 1e-12f) {
        n = DefaultNormal;
    }
    return glm::packSnorm2x16(octEncode(glm::normalize(n)));
}

}


bool GltfLoader::load(const std::filesystem::path &filepath)
{
    if (!std::filesystem::exists(filepath)) {
        core::warn("File does not exist: " + filepath.string());
        return false;
    }

    core::log("Loading glTF: " + filepath.string());

    tg3_model model;
    tg3_parse_options opts;
    tg3_error_stack errors;

    tg3_parse_options_init(&opts);
    tg3_error_stack_init(&errors);

    const std::string pathStr = filepath.string();
    const tg3_error_code parseResult =
        tg3_parse_file(&model, &errors, pathStr.c_str(), static_cast<uint32_t>(pathStr.size()), &opts);

    if (parseResult != TG3_OK) {
        std::cerr << "[error] Error parsing GLTF file:" << std::endl;
        for (uint32_t i = 0; i < errors.count; ++i) {
            std::cerr << "  " << errors.entries[i].message << std::endl;
        }
        tg3_error_stack_free(&errors);
        return false;
    }
    tg3_error_stack_free(&errors);

    const std::filesystem::path imageDir = filepath.parent_path();

    std::vector<Image> images = loadImages(model, imageDir);   // RAM
    assignImageColorSpaces(model, images);
    std::vector<uint32_t> imageIds = uploadImages(images);     // VRAM

    for (const Image &image : images) {
        stbi_image_free(image.data);
    }

    const std::vector<uint32_t> samplerIds  = loadSamplers(model);
    const std::vector<uint32_t> textureIds  = loadTextures(model, imageIds, samplerIds);
    const std::vector<uint32_t> materialIds = loadMaterials(model, textureIds);
    const std::vector<MeshHandle> meshes    = loadMeshes(model, materialIds);

    // Scene nodes.
    const int sceneIndex = model.default_scene != -1 ? model.default_scene : 0;
    const tg3_scene *scene = &model.scenes[sceneIndex];

    for (uint32_t i = 0; i < scene->nodes_count; ++i) {
        const uint32_t nodeId = importNode(model, scene->nodes[i], 0, 0, meshes);
        m_scene.addRootNode(nodeId);
    }

    tg3_model_free(&model);
    core::log("glTF loaded");
    return true;
}


uint32_t GltfLoader::importNode(const tg3_model &model, int32_t nodeIndex,
                                uint32_t parentId, uint32_t prevSiblingId,
                                const std::vector<MeshHandle> &meshes)
{
    const tg3_node &tg3Node = model.nodes[nodeIndex];

    NodeWorld &nodeWorld = m_scene.nodes();
    const uint32_t nodeId = nodeWorld.createNode().second;

    {
        Node &node = nodeWorld.getNode(nodeId);
        node.parentId = parentId;

        if (tg3Node.has_matrix) {
            glm::mat4 transform(1.0f);
            float *transPtr = glm::value_ptr(transform);
            for (uint32_t i = 0; i < 16; ++i) {
                transPtr[i] = static_cast<float>(tg3Node.matrix[i]);
            }
            node.setTransform(transform);
        } else {
            const glm::vec3 translation(tg3Node.translation[0], tg3Node.translation[1], tg3Node.translation[2]);
            // glTF stores quaternions XYZW; glm's constructor takes WXYZ.
            const glm::quat rotation(static_cast<float>(tg3Node.rotation[3]),
                                     static_cast<float>(tg3Node.rotation[0]),
                                     static_cast<float>(tg3Node.rotation[1]),
                                     static_cast<float>(tg3Node.rotation[2]));
            const glm::vec3 scale(tg3Node.scale[0], tg3Node.scale[1], tg3Node.scale[2]);

            node.setTranslation(translation);
            node.setRotation(rotation);
            node.setScale(scale);
        }

        if (tg3Node.mesh != -1 && static_cast<size_t>(tg3Node.mesh) < meshes.size()) {
            node.mesh = meshes[tg3Node.mesh];
        }
    }

    if (prevSiblingId) {
        nodeWorld.getNode(prevSiblingId).nextSiblingId = nodeId;
    }

    uint32_t lastChildId  = 0;
    uint32_t firstChildId = 0;
    for (uint32_t i = 0; i < tg3Node.children_count; ++i) {
        lastChildId = importNode(model, tg3Node.children[i], nodeId, lastChildId, meshes);
        if (!firstChildId) {
            firstChildId = lastChildId;
        }
    }
    if (firstChildId) {
        nodeWorld.getNode(nodeId).firstChildId = firstChildId;
    }

    return nodeId;
}

// meshes


std::vector<MeshHandle> GltfLoader::loadMeshes(const tg3_model &model,
                                               const std::vector<uint32_t> &materialIds)
{
    std::vector<MeshHandle> meshes(model.meshes_count);
    const VertexAttributes defaultAttributes{ .normal = encodeNormal(DefaultNormal), .uv = glm::vec2(0.0f) };

    for (uint32_t i = 0; i < model.meshes_count; ++i) {
        const tg3_mesh *tg3mesh = &model.meshes[i];

        MeshData data;
        data.name = tg3mesh->name.data != nullptr ? tg3mesh->name.data : "Unnamed mesh";
        data.subMeshes.reserve(tg3mesh->primitives_count);

        size_t totalVertices = 0;
        size_t totalIndices  = 0;
        for (uint32_t u = 0; u < tg3mesh->primitives_count; ++u) {
            const tg3_primitive &primitive = tg3mesh->primitives[u];
            if (const tg3_accessor *positions = findAttribute(model, primitive, "POSITION")) {
                totalVertices += positions->count;
                totalIndices  += primitive.indices != -1 ? model.accessors[primitive.indices].count : positions->count;
            }
        }
        data.positions.reserve(totalVertices);
        data.attributes.reserve(totalVertices);
        data.colors.reserve(totalVertices);
        data.indices.reserve(totalIndices);

        for (uint32_t u = 0; u < tg3mesh->primitives_count; ++u) {
            const tg3_primitive &primitive = tg3mesh->primitives[u];

            const tg3_accessor *positionAccessor = findAttribute(model, primitive, "POSITION");
            if (!positionAccessor || positionAccessor->count == 0) {
                core::warn(std::format("glTF mesh '{}' primitive {} has no positions; skipping", data.name, u));
                continue;
            }

            const uint64_t vertexCount = positionAccessor->count;
            const size_t   vertexStart = data.positions.size();
            const size_t   indexStart  = data.indices.size();

            data.positions.resize(vertexStart + vertexCount, glm::vec3(0.0f));
            data.attributes.resize(vertexStart + vertexCount, defaultAttributes);
            data.colors.resize(vertexStart + vertexCount, White);

            glm::vec3        *positions  = data.positions.data() + vertexStart;
            VertexAttributes *attributes = data.attributes.data() + vertexStart;
            uint32_t         *colors     = data.colors.data() + vertexStart;

            for (uint32_t v = 0; v < primitive.attributes_count; ++v) {
                const tg3_str_int_pair &attr = primitive.attributes[v];
                const tg3_accessor &accessor = model.accessors[attr.value];
                const uint64_t count = std::min(accessor.count, vertexCount);

                if (std::strcmp(attr.key.data, "POSITION") == 0) {
                    for (uint64_t vi = 0; vi < count; ++vi) {
                        positions[vi] = glm::vec3(readElement(model, accessor, vi));
                    }
                } else if (std::strcmp(attr.key.data, "NORMAL") == 0) {
                    for (uint64_t vi = 0; vi < count; ++vi) {
                        attributes[vi].normal = encodeNormal(glm::vec3(readElement(model, accessor, vi)));
                    }
                } else if (std::strcmp(attr.key.data, "TEXCOORD_0") == 0) {
                    for (uint64_t vi = 0; vi < count; ++vi) {
                        attributes[vi].uv = glm::vec2(readElement(model, accessor, vi));
                    }
                } else if (std::strcmp(attr.key.data, "COLOR_0") == 0) {
                    for (uint64_t vi = 0; vi < count; ++vi) {
                        colors[vi] = glm::packUnorm4x8(glm::clamp(readElement(model, accessor, vi), 0.0f, 1.0f));
                    }
                }
            }

            if (primitive.indices != -1) {
                const tg3_accessor &accessor = model.accessors[primitive.indices];
                data.indices.resize(indexStart + accessor.count);
                if (!readIndices(model, accessor, data.indices.data() + indexStart)) {
                    core::warn(std::format("glTF mesh '{}' primitive {} has unsupported indices; skipping", data.name, u));
                    data.positions.resize(vertexStart);
                    data.attributes.resize(vertexStart);
                    data.colors.resize(vertexStart);
                    data.indices.resize(indexStart);
                    continue;
                }
            } else {
                data.indices.resize(indexStart + vertexCount);
                std::iota(data.indices.begin() + static_cast<std::ptrdiff_t>(indexStart), data.indices.end(), 0u);
            }

            const uint32_t materialId =
                (primitive.material != -1 && static_cast<size_t>(primitive.material) < materialIds.size())
                    ? materialIds[primitive.material]
                    : 0;

            data.subMeshes.push_back(SubMesh
            {
                .vertexStart = static_cast<uint32_t>(vertexStart),
                .vertexCount = static_cast<uint32_t>(vertexCount),
                .indexStart  = static_cast<uint32_t>(indexStart),
                .indexCount  = static_cast<uint32_t>(data.indices.size() - indexStart),
                .materialIndex = m_resources.materialShaderIndex(materialId)
            });
        }

        if (!data.subMeshes.empty()) {
            meshes[i] = m_geometry.addMesh(data);
        }
    }
    return meshes;
}

// materials / textures / samplers / images

std::vector<uint32_t> GltfLoader::loadMaterials(const tg3_model &model,
                                                const std::vector<uint32_t> &textureIds)
{
    std::vector<uint32_t> materialIds(model.materials_count);

    for (uint32_t i = 0; i < model.materials_count; ++i) {
        const tg3_material *mat = &model.materials[i];
        const int32_t texIndex = mat->pbr_metallic_roughness.base_color_texture.index;

        const uint32_t textureId =
            (texIndex != -1 && static_cast<size_t>(texIndex) < textureIds.size())
                ? textureIds[texIndex]
                : m_resources.fallbackTextureId();

        materialIds[i] = m_resources.addMaterial(Material
        {
            .baseColor = glm::vec4(
                mat->pbr_metallic_roughness.base_color_factor[0],
                mat->pbr_metallic_roughness.base_color_factor[1],
                mat->pbr_metallic_roughness.base_color_factor[2],
                mat->pbr_metallic_roughness.base_color_factor[3]),

            // Material::textureIndex is the 0-based descriptor slot the
            // shader samples. ResourceStore owns that conversion.
            .textureIndex = m_resources.textureDescriptorSlot(textureId),
            .samplerIndex = m_resources.samplerDescriptorSlot(textureId)
        });
    }
    return materialIds;
}

std::vector<uint32_t> GltfLoader::loadTextures(const tg3_model &model,
                                               const std::vector<uint32_t> &imageIds,
                                               const std::vector<uint32_t> &samplerIds)
{
    std::vector<uint32_t> textureIds(model.textures_count);

    for (uint32_t i = 0; i < model.textures_count; ++i) {
        const tg3_texture &tex = model.textures[i];

        // Both source and sampler are optional in glTF and come back as -1.
        // Indexing the vectors with those was reading out of bounds.
        const uint32_t imageId =
            (tex.source != -1 && static_cast<size_t>(tex.source) < imageIds.size())
                ? imageIds[tex.source]
                : m_resources.fallbackImageId();

        const uint32_t samplerId =
            (tex.sampler != -1 && static_cast<size_t>(tex.sampler) < samplerIds.size())
                ? samplerIds[tex.sampler]
                : m_resources.fallbackSamplerId();

        textureIds[i] = m_resources.addTexture(imageId, samplerId);
    }
    return textureIds;
}

std::vector<uint32_t> GltfLoader::loadSamplers(const tg3_model &model)
{
    // filter -> { magFilter/minFilter, mipmapMode, maxLod }
    static const std::unordered_map<int32_t, std::tuple<VkFilter, VkSamplerMipmapMode, float>> filterMap
    {
        { TG3_TEXTURE_FILTER_NEAREST,                { VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.25f } },
        { TG3_TEXTURE_FILTER_LINEAR,                 { VK_FILTER_LINEAR,  VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.25f } },
        { TG3_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR,   { VK_FILTER_LINEAR,  VK_SAMPLER_MIPMAP_MODE_LINEAR,  VK_LOD_CLAMP_NONE } },
        { TG3_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST, { VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_LOD_CLAMP_NONE } },
        { TG3_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR,  { VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_LINEAR,  VK_LOD_CLAMP_NONE } },
        { TG3_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST,  { VK_FILTER_LINEAR,  VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_LOD_CLAMP_NONE } }
    };

    static const std::unordered_map<int32_t, VkSamplerAddressMode> wrapMap
    {
        { TG3_TEXTURE_WRAP_REPEAT,          VK_SAMPLER_ADDRESS_MODE_REPEAT },
        { TG3_TEXTURE_WRAP_CLAMP_TO_EDGE,   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE },
        { TG3_TEXTURE_WRAP_MIRRORED_REPEAT, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT }
    };

    // .at() throws on an unknown enum value; look up defensively instead.
    auto filterOr = [](int32_t key, auto fallback, auto extractor) {
        const auto it = filterMap.find(key);
        return it == filterMap.end() ? fallback : extractor(it->second);
    };
    auto wrapOr = [](int32_t key) {
        const auto it = wrapMap.find(key);
        return it == wrapMap.end() ? VK_SAMPLER_ADDRESS_MODE_REPEAT : it->second;
    };

    std::vector<uint32_t> samplerIds(model.samplers_count);

    for (uint32_t i = 0; i < model.samplers_count; ++i) {
        const tg3_sampler &tg3Sampler = model.samplers[i];

        VkSamplerCreateInfo samplerInfo
        {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter  = filterOr(tg3Sampler.mag_filter, VK_FILTER_LINEAR,
                                   [](const auto &t) { return std::get<0>(t); }),
            .minFilter  = filterOr(tg3Sampler.min_filter, VK_FILTER_LINEAR,
                                   [](const auto &t) { return std::get<0>(t); }),
            .mipmapMode = filterOr(tg3Sampler.min_filter, VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                   [](const auto &t) { return std::get<1>(t); }),
            .addressModeU = wrapOr(tg3Sampler.wrap_s),
            .addressModeV = wrapOr(tg3Sampler.wrap_t),
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,   // change for 3D textures
            .compareEnable = VK_FALSE,
            .minLod = 0.0f,
            .maxLod = filterOr(tg3Sampler.min_filter, VK_LOD_CLAMP_NONE,
                               [](const auto &t) { return std::get<2>(t); })
        };

        const uint32_t samplerId = m_resources.addSampler(samplerInfo);
        samplerIds[i] = samplerId ? samplerId : m_resources.fallbackSamplerId();
    }
    return samplerIds;
}

void GltfLoader::assignImageColorSpaces(const tg3_model &model, std::vector<Image> &images) const
{
    // Anything no material references keeps the linear default; it is never
    // sampled anyway.
    auto markSrgb = [&](int32_t textureIndex) {
        if (textureIndex < 0 || static_cast<uint32_t>(textureIndex) >= model.textures_count) {
            return;
        }
        const int32_t source = model.textures[textureIndex].source;
        if (source >= 0 && static_cast<size_t>(source) < images.size()) {
            images[source].format = VK_FORMAT_R8G8B8A8_SRGB;
        }
    };

    for (uint32_t i = 0; i < model.materials_count; ++i) {
        // Only base colour and emissive are colour. Metallic-roughness,
        // normal and occlusion are all data.
        markSrgb(model.materials[i].pbr_metallic_roughness.base_color_texture.index);
        markSrgb(model.materials[i].emissive_texture.index);
    }
}


std::vector<Image> GltfLoader::loadImages(const tg3_model &model,
                                          const std::filesystem::path &imageDir) const
{
    std::vector<Image> images(model.images_count);

    for (uint32_t i = 0; i < model.images_count; ++i) {
        Image &img = images[i];

        // Embedded/buffer-view images have no URI. Those aren't handled yet;
        // they fall through to the purple fallback rather than crashing.
        if (!model.images[i].uri.data) {
            core::warn("glTF image has no URI (embedded images are not supported yet)");
            continue;
        }

        const std::filesystem::path imagePath = imageDir / model.images[i].uri.data;
        core::log(std::format("Loading image {} / {}: {} ", i + 1, model.images_count, model.images[i].uri.data));

        img.data = stbi_load(imagePath.string().c_str(), &img.width, &img.height, &img.channels, 4);
        if (!img.data) {
            core::warn("Failed to load image: " + imagePath.string());
        }
    }
    return images;
}

std::vector<uint32_t> GltfLoader::uploadImages(const std::vector<Image> &images)
{
    std::vector<uint32_t> imageIds(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        const Image &image = images[i];
        imageIds[i] = image.data
            ? m_resources.addImage(image.data, static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height),image.format)
            : m_resources.fallbackImageId();
    }
    return imageIds;
}