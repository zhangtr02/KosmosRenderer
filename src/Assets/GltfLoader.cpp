#include "Assets/GltfLoader.h"

#include <stdexcept>

namespace kosmos::assets
{
scene::Scene GltfLoader::LoadStaticScene(const std::filesystem::path& path) const
{
    if (path.empty())
    {
        throw std::invalid_argument("glTF path is empty.");
    }

    throw std::runtime_error("glTF loading is scheduled for Phase 3. The scene API is ready for it.");
}
}
