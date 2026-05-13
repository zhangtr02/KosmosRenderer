#pragma once

#include "Scene/Scene.h"

#include <filesystem>

namespace kosmos::assets
{
class GltfLoader
{
public:
    scene::Scene LoadStaticScene(const std::filesystem::path& path) const;
};
}
