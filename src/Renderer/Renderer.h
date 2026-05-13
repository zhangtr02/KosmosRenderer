#pragma once

#include <memory>
#include <string>

namespace kosmos::platform
{
class Window;
}

namespace kosmos::scene
{
class Scene;
}

namespace kosmos::renderer
{
struct RendererConfig
{
    std::string applicationName = "KosmosRenderer";
    unsigned int initialWidth = 1280;
    unsigned int initialHeight = 720;
    bool enableValidation = true;
};

class Renderer
{
public:
    virtual ~Renderer() = default;

    virtual void Initialize(platform::Window& window, const RendererConfig& config) = 0;
    virtual void Resize(unsigned int width, unsigned int height) = 0;
    virtual bool BeginFrame() = 0;
    virtual void RenderScene(const scene::Scene& scene) = 0;
    virtual void EndFrame() = 0;
    virtual void WaitIdle() = 0;
    virtual void Shutdown() = 0;
};

std::unique_ptr<Renderer> CreateVulkanRenderer();
}
