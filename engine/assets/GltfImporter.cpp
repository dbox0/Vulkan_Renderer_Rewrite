#include "ImportedScene.h"

#include <algorithm>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <format>
#include <numeric>
#include <optional>
#include <string_view>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <stb_image.h>
#include <tiny_gltf_v3.h>

#include "Mips.h"
#include "core/File.h"
#include "core/Parallel.h"
#include "core/Uri.h"

namespace assets {

namespace
{

using Clock = std::chrono::steady_clock;

const glm::vec3 DefaultNormal(0.0f, 1.0f, 0.0f);
constexpr uint32_t White = 0xFFFFFFFFu;

double millisecondsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string_view view(const tg3_str &str)
{
    return str.data ? std::string_view(str.data, str.len) : std::string_view();
}

std::string nameOr(const tg3_str &str, std::string_view fallback)
{
    const std::string_view name = view(str);
    return std::string(name.empty() ? fallback : name);
}

struct ModelGuard
{
    tg3_model &model;
    ~ModelGuard() { tg3_model_free(&model); }
};

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


Filter toFilter(int32_t gltfFilter)
{
    switch (gltfFilter) {
    case TG3_TEXTURE_FILTER_NEAREST:
    case TG3_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
    case TG3_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
        return Filter::Nearest;
    default:
        return Filter::Linear;
    }
}

Wrap toWrap(int32_t gltfWrap)
{
    switch (gltfWrap) {
    case TG3_TEXTURE_WRAP_CLAMP_TO_EDGE:   return Wrap::ClampToEdge;
    case TG3_TEXTURE_WRAP_MIRRORED_REPEAT: return Wrap::MirroredRepeat;
    default:                               return Wrap::Repeat;
    }
}

ImportedSampler convertSampler(const tg3_sampler &sampler)
{
    ImportedSampler out;
    out.magFilter = toFilter(sampler.mag_filter);
    out.minFilter = toFilter(sampler.min_filter);
    switch (sampler.min_filter) {
    case TG3_TEXTURE_FILTER_NEAREST:
    case TG3_TEXTURE_FILTER_LINEAR:
        out.useMips = false;
        break;
    case TG3_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
    case TG3_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
        out.mipFilter = Filter::Nearest;
        break;
    default:
        out.mipFilter = Filter::Linear;
        break;
    }
    out.wrapU = toWrap(sampler.wrap_s);
    out.wrapV = toWrap(sampler.wrap_t);
    return out;
}

ImportedMaterial convertMaterial(const tg3_material &material)
{
    const tg3_pbr_metallic_roughness &pbr = material.pbr_metallic_roughness;
    const std::string_view alphaMode = view(material.alpha_mode);

    ImportedMaterial out;
    out.name = nameOr(material.name, "Material");
    out.baseColorFactor = glm::vec4(pbr.base_color_factor[0], pbr.base_color_factor[1],
                                    pbr.base_color_factor[2], pbr.base_color_factor[3]);
    out.baseColorTexture = pbr.base_color_texture.index;
    out.metallicFactor = static_cast<float>(pbr.metallic_factor);
    out.roughnessFactor = static_cast<float>(pbr.roughness_factor);
    out.metallicRoughnessTexture = pbr.metallic_roughness_texture.index;
    out.normalTexture = material.normal_texture.index;
    out.normalScale = static_cast<float>(material.normal_texture.scale);
    out.occlusionTexture = material.occlusion_texture.index;
    out.occlusionStrength = static_cast<float>(material.occlusion_texture.strength);
    out.emissiveFactor = glm::vec3(material.emissive_factor[0], material.emissive_factor[1], material.emissive_factor[2]);
    out.emissiveTexture = material.emissive_texture.index;
    out.alphaMode = alphaMode == "MASK" ? AlphaMode::Mask : alphaMode == "BLEND" ? AlphaMode::Blend : AlphaMode::Opaque;
    out.alphaCutoff = static_cast<float>(material.alpha_cutoff);
    out.doubleSided = material.double_sided != 0;
    return out;
}

ImportedLight convertLight(const tg3_light &light)
{
    const std::string_view type = view(light.type);
    ImportedLight out;
    out.type = type == "directional" ? LightType::Directional : type == "spot" ? LightType::Spot : LightType::Point;
    out.color = glm::vec3(light.color[0], light.color[1], light.color[2]);
    out.intensity = static_cast<float>(light.intensity);
    out.range = static_cast<float>(light.range);
    out.innerCone = static_cast<float>(light.spot.inner_cone_angle);
    out.outerCone = static_cast<float>(light.spot.outer_cone_angle);
    return out;
}

ImportedCamera convertCamera(const tg3_camera &camera)
{
    ImportedCamera out;
    out.perspective = view(camera.type) == "perspective";
    out.yFov = static_cast<float>(camera.perspective.yfov);
    out.zNear = static_cast<float>(camera.perspective.znear);
    out.zFar = static_cast<float>(camera.perspective.zfar);
    return out;
}

core::Transform convertTransform(const tg3_node &node, std::string_view name, std::vector<std::string> &warnings)
{
    core::Transform local;
    if (node.has_matrix) {
        glm::mat4 matrix(1.0f);
        float *matrixPtr = glm::value_ptr(matrix);
        for (uint32_t i = 0; i < 16; ++i) {
            matrixPtr[i] = static_cast<float>(node.matrix[i]);
        }
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(matrix, local.scale, local.rotation, local.translation, skew, perspective);
        if (glm::dot(skew, skew) > 1e-8f || glm::abs(perspective - glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)) != glm::vec4(0.0f)) {
            warnings.push_back(std::format("Node '{}' has a skewed or projective matrix; only translation, rotation and scale are kept", name));
        }
    } else {
        local.translation = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
        local.rotation = glm::quat(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
                                   static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2]));
        local.scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
    }
    return local;
}

MeshData convertMesh(const tg3_model &model, const tg3_mesh &gltfMesh, std::vector<std::string> &warnings)
{
    const PackedAttributes defaultAttributes{ .normal = encodeNormal(DefaultNormal), .uv = glm::vec2(0.0f) };

    MeshData data;
    data.name = nameOr(gltfMesh.name, "Mesh");
    data.subMeshes.reserve(gltfMesh.primitives_count);

    size_t totalVertices = 0;
    size_t totalIndices  = 0;
    for (uint32_t u = 0; u < gltfMesh.primitives_count; ++u) {
        const tg3_primitive &primitive = gltfMesh.primitives[u];
        if (const tg3_accessor *positions = findAttribute(model, primitive, "POSITION")) {
            totalVertices += positions->count;
            totalIndices  += primitive.indices != -1 ? model.accessors[primitive.indices].count : positions->count;
        }
    }
    data.positions.reserve(totalVertices);
    data.attributes.reserve(totalVertices);
    data.colors.reserve(totalVertices);
    data.indices.reserve(totalIndices);

    bool warnedMode = false;
    for (uint32_t u = 0; u < gltfMesh.primitives_count; ++u) {
        const tg3_primitive &primitive = gltfMesh.primitives[u];

        if (primitive.mode != -1 && primitive.mode != TG3_MODE_TRIANGLES) {
            if (!warnedMode) {
                warnings.push_back(std::format("Mesh '{}': primitives that are not triangle lists are skipped", data.name));
                warnedMode = true;
            }
            continue;
        }

        const tg3_accessor *positionAccessor = findAttribute(model, primitive, "POSITION");
        if (!positionAccessor || positionAccessor->count == 0) {
            warnings.push_back(std::format("Mesh '{}' primitive {} has no positions; skipped", data.name, u));
            continue;
        }

        const uint64_t vertexCount = positionAccessor->count;
        const size_t   vertexStart = data.positions.size();
        const size_t   indexStart  = data.indices.size();

        data.positions.resize(vertexStart + vertexCount, glm::vec3(0.0f));
        data.attributes.resize(vertexStart + vertexCount, defaultAttributes);
        data.colors.resize(vertexStart + vertexCount, White);

        glm::vec3        *positions  = data.positions.data() + vertexStart;
        PackedAttributes *attributes = data.attributes.data() + vertexStart;
        uint32_t         *colors     = data.colors.data() + vertexStart;

        glm::vec3 aabbMin(FLT_MAX);
        glm::vec3 aabbMax(-FLT_MAX);
        for (uint32_t v = 0; v < primitive.attributes_count; ++v) {
            const tg3_str_int_pair &attr = primitive.attributes[v];
            const tg3_accessor &accessor = model.accessors[attr.value];
            const uint64_t count = std::min(accessor.count, vertexCount);
            const std::string_view key = view(attr.key);

            if (key == "POSITION") {
                for (uint64_t vi = 0; vi < count; ++vi) {
                    positions[vi] = glm::vec3(readElement(model, accessor, vi));
                    aabbMin = glm::min(aabbMin, positions[vi]);
                    aabbMax = glm::max(aabbMax, positions[vi]);
                }
            } else if (key == "NORMAL") {
                for (uint64_t vi = 0; vi < count; ++vi) {
                    attributes[vi].normal = encodeNormal(glm::vec3(readElement(model, accessor, vi)));
                }
            } else if (key == "TEXCOORD_0") {
                for (uint64_t vi = 0; vi < count; ++vi) {
                    attributes[vi].uv = glm::vec2(readElement(model, accessor, vi));
                }
            } else if (key == "COLOR_0") {
                for (uint64_t vi = 0; vi < count; ++vi) {
                    colors[vi] = glm::packUnorm4x8(glm::clamp(readElement(model, accessor, vi), 0.0f, 1.0f));
                }
            }
        }

        if (primitive.indices != -1) {
            const tg3_accessor &accessor = model.accessors[primitive.indices];
            data.indices.resize(indexStart + accessor.count);
            if (!readIndices(model, accessor, data.indices.data() + indexStart)) {
                warnings.push_back(std::format("Mesh '{}' primitive {} has unsupported indices; skipped", data.name, u));
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

        const glm::vec3 centre = (aabbMin + aabbMax) * 0.5f;
        float radiusSquared = 0.0f;
        for (uint64_t vi = 0; vi < vertexCount; ++vi) {
            const glm::vec3 offset = positions[vi] - centre;
            radiusSquared = std::max(radiusSquared, glm::dot(offset, offset));
        }

        data.subMeshes.push_back(SubMesh
        {
            .vertexStart = static_cast<uint32_t>(vertexStart),
            .vertexCount = static_cast<uint32_t>(vertexCount),
            .indexStart  = static_cast<uint32_t>(indexStart),
            .indexCount  = static_cast<uint32_t>(data.indices.size() - indexStart),
            .material    = primitive.material,
            .sphere      = glm::vec4(centre, std::sqrt(radiusSquared)),
            .aabbMin     = aabbMin,
            .aabbMax     = aabbMax
        });
    }
    return data;
}

void assignColorSpaces(const tg3_model &model, std::vector<ImportedImage> &images, std::vector<std::string> &warnings)
{
    std::vector<int8_t> usage(images.size(), -1);
    const auto mark = [&](int32_t textureIndex, ColorSpace space) {
        if (textureIndex < 0 || static_cast<uint32_t>(textureIndex) >= model.textures_count) {
            return;
        }
        const int32_t source = model.textures[textureIndex].source;
        if (source < 0 || static_cast<size_t>(source) >= images.size()) {
            return;
        }
        const auto index = static_cast<size_t>(source);
        if (usage[index] == -1) {
            usage[index] = static_cast<int8_t>(space);
            images[index].colorSpace = space;
        } else if (usage[index] != static_cast<int8_t>(space)) {
            warnings.push_back(std::format("Image '{}' is used both as color and as data; keeping its first use",
                                           images[index].name));
            usage[index] = static_cast<int8_t>(images[index].colorSpace);
        }
    };

    for (uint32_t i = 0; i < model.materials_count; ++i) {
        const tg3_material &material = model.materials[i];
        mark(material.pbr_metallic_roughness.base_color_texture.index, ColorSpace::Srgb);
        mark(material.emissive_texture.index, ColorSpace::Srgb);
        mark(material.pbr_metallic_roughness.metallic_roughness_texture.index, ColorSpace::Linear);
        mark(material.normal_texture.index, ColorSpace::Linear);
        mark(material.occlusion_texture.index, ColorSpace::Linear);
    }
}

struct EncodedImage
{
    std::vector<std::byte> owned;
    const std::byte       *data = nullptr;
    size_t                 size = 0;
};

std::optional<EncodedImage> readEncodedImage(const tg3_model &model, const tg3_image &image,
                                             const std::filesystem::path &baseDir, std::string &error)
{
    EncodedImage encoded;
    if (image.buffer_view >= 0) {
        const tg3_buffer_view &bufferView = model.buffer_views[image.buffer_view];
        const tg3_buffer      &buffer     = model.buffers[bufferView.buffer];
        if (bufferView.byte_offset + bufferView.byte_length > buffer.data.count) {
            error = "its buffer view runs past the end of the buffer";
            return std::nullopt;
        }
        encoded.data = reinterpret_cast<const std::byte *>(buffer.data.data + bufferView.byte_offset);
        encoded.size = static_cast<size_t>(bufferView.byte_length);
        return encoded;
    }

    const std::string_view uri = view(image.uri);
    if (uri.empty()) {
        error = "it has neither a URI nor a buffer view";
        return std::nullopt;
    }

    if (uri.starts_with("data:")) {
        constexpr std::string_view Marker = ";base64,";
        const size_t marker = uri.find(Marker);
        if (marker == std::string_view::npos) {
            error = "its data URI is not base64";
            return std::nullopt;
        }
        std::optional<std::vector<std::byte>> bytes = core::base64Decode(uri.substr(marker + Marker.size()));
        if (!bytes) {
            error = "its data URI is not valid base64";
            return std::nullopt;
        }
        encoded.owned = std::move(*bytes);
    } else {
        const std::filesystem::path path = baseDir / std::filesystem::path(core::percentDecode(uri));
        std::optional<std::vector<std::byte>> bytes = core::readFile(path);
        if (!bytes) {
            error = std::format("{} could not be read", path.string());
            return std::nullopt;
        }
        encoded.owned = std::move(*bytes);
    }
    encoded.data = encoded.owned.data();
    encoded.size = encoded.owned.size();
    return encoded;
}

void decodeImages(const tg3_model &model, const std::filesystem::path &baseDir,
                  std::vector<ImportedImage> &images, std::vector<std::string> &warnings)
{
    std::vector<std::string> errors(images.size());
    core::parallelFor(static_cast<uint32_t>(images.size()), [&](uint32_t i) {
        ImportedImage &image = images[i];
        const std::optional<EncodedImage> encoded = readEncodedImage(model, model.images[i], baseDir, errors[i]);
        if (!encoded) {
            return;
        }
        if (encoded->size > static_cast<size_t>(INT32_MAX)) {
            errors[i] = "it is too large";
            return;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc *pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(encoded->data),
                                                static_cast<int>(encoded->size), &width, &height, &channels, 4);
        if (!pixels) {
            errors[i] = std::format("stb_image could not decode it ({})", stbi_failure_reason());
            return;
        }
        image.mips = generateMips(pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height), image.colorSpace);
        stbi_image_free(pixels);
    });

    for (size_t i = 0; i < errors.size(); ++i) {
        if (!errors[i].empty()) {
            warnings.push_back(std::format("Image '{}' not loaded: {}", images[i].name, errors[i]));
        }
    }
}

void collectNodes(const tg3_model &model, ImportedScene &out, std::vector<std::string> &warnings)
{
    if (model.scenes_count == 0) {
        return;
    }
    const int32_t sceneIndex = model.default_scene >= 0 ? model.default_scene : 0;
    const tg3_scene &gltfScene = model.scenes[sceneIndex];

    std::vector<std::pair<int32_t, int32_t>> stack;
    for (uint32_t r = gltfScene.nodes_count; r-- > 0;) {
        stack.emplace_back(gltfScene.nodes[r], -1);
    }

    while (!stack.empty()) {
        const auto [gltfIndex, parent] = stack.back();
        stack.pop_back();

        const tg3_node &gltfNode = model.nodes[gltfIndex];
        const auto self = static_cast<int32_t>(out.nodes.size());

        ImportedNode node;
        node.name = nameOr(gltfNode.name, "Node");
        node.parent = parent;
        node.local = convertTransform(gltfNode, node.name, warnings);
        if (gltfNode.mesh >= 0 && static_cast<size_t>(gltfNode.mesh) < out.meshes.size()) {
            node.mesh = gltfNode.mesh;
        }
        if (gltfNode.light >= 0 && static_cast<size_t>(gltfNode.light) < out.lights.size()) {
            node.light = gltfNode.light;
        }
        if (gltfNode.camera >= 0 && static_cast<size_t>(gltfNode.camera) < out.cameras.size()) {
            node.camera = gltfNode.camera;
        }
        out.nodes.push_back(std::move(node));

        for (uint32_t c = gltfNode.children_count; c-- > 0;) {
            stack.emplace_back(gltfNode.children[c], self);
        }
    }
}

}

ImportResult importGltf(const std::filesystem::path &path)
{
    ImportResult result;
    const Clock::time_point parseStart = Clock::now();

    tg3_model model{};
    const ModelGuard guard{ model };

    tg3_parse_options options;
    tg3_parse_options_init(&options);
    options.images_as_is = 1;

    tg3_error_stack errors;
    tg3_error_stack_init(&errors);

    const std::string pathString = path.string();
    const tg3_error_code code = tg3_parse_file(&model, &errors, pathString.c_str(),
                                               static_cast<uint32_t>(pathString.size()), &options);
    for (uint32_t i = 0; i < errors.count; ++i) {
        if (code != TG3_OK || errors.entries[i].severity == TG3_SEVERITY_ERROR) {
            result.warnings.push_back(std::format("{}: {}", path.filename().string(), errors.entries[i].message));
        }
    }
    tg3_error_stack_free(&errors);
    if (code != TG3_OK) {
        result.warnings.push_back(std::format("Could not parse {}", pathString));
        return result;
    }
    result.parseMs = millisecondsSince(parseStart);

    ImportedScene out;

    out.samplers.reserve(model.samplers_count);
    for (uint32_t i = 0; i < model.samplers_count; ++i) {
        out.samplers.push_back(convertSampler(model.samplers[i]));
    }
    out.textures.reserve(model.textures_count);
    for (uint32_t i = 0; i < model.textures_count; ++i) {
        out.textures.push_back(ImportedTexture{ .image = model.textures[i].source, .sampler = model.textures[i].sampler });
    }
    out.materials.reserve(model.materials_count);
    for (uint32_t i = 0; i < model.materials_count; ++i) {
        out.materials.push_back(convertMaterial(model.materials[i]));
    }
    out.lights.reserve(model.lights_count);
    for (uint32_t i = 0; i < model.lights_count; ++i) {
        out.lights.push_back(convertLight(model.lights[i]));
    }
    out.cameras.reserve(model.cameras_count);
    for (uint32_t i = 0; i < model.cameras_count; ++i) {
        out.cameras.push_back(convertCamera(model.cameras[i]));
    }

    const Clock::time_point meshStart = Clock::now();
    out.meshes.resize(model.meshes_count);
    std::vector<std::vector<std::string>> meshWarnings(model.meshes_count);
    core::parallelFor(model.meshes_count, [&](uint32_t i) {
        out.meshes[i] = convertMesh(model, model.meshes[i], meshWarnings[i]);
    });
    for (std::vector<std::string> &warnings : meshWarnings) {
        result.warnings.insert(result.warnings.end(), std::make_move_iterator(warnings.begin()),
                               std::make_move_iterator(warnings.end()));
    }
    result.meshesMs = millisecondsSince(meshStart);

    const Clock::time_point imageStart = Clock::now();
    out.images.resize(model.images_count);
    for (uint32_t i = 0; i < model.images_count; ++i) {
        const std::string_view uri = view(model.images[i].uri);
        const std::string fallback = uri.empty() || uri.starts_with("data:") ? std::format("Image {}", i) : std::string(uri);
        out.images[i].name = nameOr(model.images[i].name, fallback);
    }
    assignColorSpaces(model, out.images, result.warnings);
    decodeImages(model, path.parent_path(), out.images, result.warnings);
    result.imagesMs = millisecondsSince(imageStart);

    collectNodes(model, out, result.warnings);

    result.scene = std::move(out);
    return result;
}

}
