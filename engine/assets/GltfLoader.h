#pragma once
#include <filesystem>
#include <vector>
#include <cstdint>

#include "core/Handle.h"

struct tg3_model;

namespace gfx { class Context; }
class ResourceStore;
class GeometryStore;
class Scene;
struct Image;

// Translates a parsed glTF document into the stores. Contains no Vulkan calls
// goes through ResourceStore / GeometryStore for everything.
class GltfLoader
{
public:
    GltfLoader(gfx::Context &ctx, ResourceStore &resources,
               GeometryStore &geometry, Scene &scene)
        : m_ctx(ctx), m_resources(resources), m_geometry(geometry), m_scene(scene) {}

    bool load(const std::filesystem::path &filepath);

private:
    std::vector<Image>    loadImages(const tg3_model &model, const std::filesystem::path &imageDir) const;
    std::vector<uint32_t> uploadImages(const std::vector<Image> &images);
    std::vector<uint32_t> loadSamplers(const tg3_model &model);

    void assignImageColorSpaces(const tg3_model &model, std::vector<Image> &images) const;

    std::vector<uint32_t> loadTextures(const tg3_model &model,
                                       const std::vector<uint32_t> &imageIds,
                                       const std::vector<uint32_t> &samplerIds);
    std::vector<uint32_t> loadMaterials(const tg3_model &model,
                                        const std::vector<uint32_t> &textureIds);
    std::vector<MeshHandle> loadMeshes(const tg3_model &model,
                                       const std::vector<uint32_t> &materialIds);

    uint32_t importNode(const tg3_model &model, int32_t nodeIndex,
                        uint32_t parentId, uint32_t prevSiblingId,
                        const std::vector<MeshHandle> &meshes);

    gfx::Context  &m_ctx;
    ResourceStore &m_resources;
    GeometryStore &m_geometry;
    Scene         &m_scene;
};