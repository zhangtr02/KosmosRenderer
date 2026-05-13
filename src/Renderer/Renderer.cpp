#include "Renderer/Renderer.h"

#include "Renderer/Vulkan/VulkanRenderer.h"

namespace kosmos::renderer
{
std::unique_ptr<Renderer> CreateVulkanRenderer()
{
    return std::make_unique<VulkanRenderer>();
}
}
