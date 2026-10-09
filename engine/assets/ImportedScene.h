#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "MeshData.h"
#include "core/Transform.h"

namespace assets {
    enum class ColorSpace : uint8_t { Linear, Srgb };
    enum class Filter     : uint8_t { Nearest, Linear };
    enum class Wrap       : uint8_t { Repeat, ClampToEdge, MirroredRepeat };
    enum class AlphaMode  : uint8_t { Opaque, Mask, Blend };
    enum class LightType  : uint8_t { Directional, Point, Spot };

    struct MipLevel
    {
        uint32_t             width  = 0;
        uint32_t             height = 0;
        std::vector<uint8_t> rgba;
    };

    struct ImportedImage
    {
        std::string           name;
        ColorSpace            colorSpace = ColorSpace::Linear;
        std::vector<MipLevel> mips;
    };

    struct ImportedSampler
    {
        Filter magFilter = Filter::Linear;
        Filter minFilter = Filter::Linear;
        Filter mipFilter = Filter::Linear;
        bool   useMips   = true;
        Wrap   wrapU     = Wrap::Repeat;
        Wrap   wrapV     = Wrap::Repeat;
    };

    struct ImportedTexture
    {
        int32_t image   = -1;
        int32_t sampler = -1;
    };

    struct ImportedMaterial
    {
        std::string name;
        glm::vec4   baseColorFactor{ 1.0f };
        int32_t     baseColorTexture = -1;
        float       metallicFactor  = 1.0f;
        float       roughnessFactor = 1.0f;
        int32_t     metallicRoughnessTexture = -1;
        int32_t     normalTexture = -1;
        float       normalScale = 1.0f;
        int32_t     occlusionTexture = -1;
        float       occlusionStrength = 1.0f;
        glm::vec3   emissiveFactor{ 0.0f };
        int32_t     emissiveTexture = -1;
        AlphaMode   alphaMode = AlphaMode::Opaque;
        float       alphaCutoff = 0.5f;
        bool        doubleSided = false;
    };

    struct ImportedLight
    {
        LightType type      = LightType::Point;
        glm::vec3 color{ 1.0f };
        float     intensity = 1.0f;
        float     range     = 0.0f;
        float     innerCone = 0.0f;
        float     outerCone = 0.785398163f;
    };

    struct ImportedCamera
    {
        bool  perspective = false;
        float yFov  = 0.0f;
        float zNear = 0.0f;
        float zFar  = 0.0f;
    };

    struct ImportedNode
    {
        std::string     name;
        int32_t         parent = -1;
        core::Transform local;
        int32_t         mesh   = -1;
        int32_t         light  = -1;
        int32_t         camera = -1;
    };

    struct ImportedScene
    {
        std::vector<ImportedImage>    images;
        std::vector<ImportedSampler>  samplers;
        std::vector<ImportedTexture>  textures;
        std::vector<ImportedMaterial> materials;
        std::vector<MeshData>         meshes;
        std::vector<ImportedNode>     nodes;
        std::vector<ImportedLight>    lights;
        std::vector<ImportedCamera>   cameras;
    };

    struct ImportResult
    {
        std::optional<ImportedScene> scene;
        std::vector<std::string>     warnings;
        double parseMs  = 0.0;
        double meshesMs = 0.0;
        double imagesMs = 0.0;
    };

    ImportResult importGltf(const std::filesystem::path &path);
}
