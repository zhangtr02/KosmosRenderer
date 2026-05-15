#pragma once

#include "App/FlyCameraController.h"
#include "Platform/Clock.h"
#include "Platform/Window.h"
#include "Renderer/Renderer.h"
#include "Scene/Scene.h"

#include <memory>
#include <filesystem>
#include <string>

namespace kosmos::app
{
struct ApplicationConfig
{
    std::string title = "KosmosRenderer";
    int width = 1280;
    int height = 720;
    unsigned int maxFrames = 0;
    std::filesystem::path scenePath;
};

class Application
{
public:
    explicit Application(ApplicationConfig config = {});

    int Run();

private:
    void Initialize();
    void Shutdown();
    void Tick(float deltaSeconds);

    ApplicationConfig config_;
    platform::Window window_;
    platform::Clock clock_;
    scene::Scene scene_;
    FlyCameraController cameraController_;
    std::unique_ptr<renderer::Renderer> renderer_;
    renderer::DebugRenderMode debugRenderMode_ = renderer::DebugRenderMode::Lit;
    unsigned int renderedFrames_ = 0;
    bool initialized_ = false;
};
}
