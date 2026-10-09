#pragma once
#include <string_view>

#include "assets/ImportedScene.h"
#include "scene/SceneTypes.h"

namespace scene { class Scene; }
class GeometryStore;
class ResourceStore;

namespace render
{
    scene::NodeHandle instantiate(const assets::ImportedScene &imported, std::string_view rootName,
                                  scene::Scene &scene, ResourceStore &resources, GeometryStore &geometry);
}
