#include "App/Application.h"

#include "Assets/GltfLoader.h"
#include "Renderer/Renderer.h"

#include <iostream>

namespace kosmos::app
{
Application::Application(ApplicationConfig config)
    : config_(std::move(config))
{
}

int Application::Run()
{
    Initialize();

    while (!window_.ShouldClose())
    {
        const float deltaSeconds = clock_.Tick();
        window_.PollEvents();
        Tick(deltaSeconds);

        if (window_.ConsumeFramebufferResize())
        {
            const auto size = window_.GetFramebufferSize();
            renderer_->Resize(size.width, size.height);
        }

        if (renderer_->BeginFrame())
        {
            renderer_->RenderScene(scene_);
            renderer_->EndFrame();
            ++renderedFrames_;
        }

        if (config_.maxFrames > 0 && renderedFrames_ >= config_.maxFrames)
        {
            window_.RequestClose();
        }
    }

    Shutdown();
    return 0;
}

void Application::Initialize()
{
    if (initialized_)
    {
        return;
    }

    window_.Initialize(config_.width, config_.height, config_.title);
    if (!config_.scenePath.empty())
    {
        scene_ = assets::GltfLoader{}.LoadStaticScene(config_.scenePath);
    }
    else
    {
        scene_ = scene::Scene::CreateDemoScene();
    }

    renderer::RendererConfig rendererConfig;
    rendererConfig.applicationName = config_.title;
    rendererConfig.enableValidation = KOSMOS_ENABLE_VALIDATION != 0;
    rendererConfig.initialWidth = static_cast<unsigned int>(config_.width);
    rendererConfig.initialHeight = static_cast<unsigned int>(config_.height);

    renderer_ = renderer::CreateVulkanRenderer();
    renderer_->Initialize(window_, rendererConfig);
    renderer_->SetDebugRenderMode(debugRenderMode_);

    std::cout << "KosmosRenderer started. Hold RMB to fly, WASD to move, Q/E to descend/ascend. Press 1-6 for render debug modes.\n";
    renderedFrames_ = 0;
    initialized_ = true;
}

void Application::Shutdown()
{
    if (!initialized_)
    {
        return;
    }

    renderer_->WaitIdle();
    renderer_->Shutdown();
    renderer_.reset();
    window_.Shutdown();
    initialized_ = false;
}

void Application::Tick(float deltaSeconds)
{
    if (window_.IsKeyPressed(platform::Key::Escape))
    {
        window_.RequestClose();
    }

    const auto setDebugMode = [this](renderer::DebugRenderMode mode) {
        if (debugRenderMode_ != mode)
        {
            debugRenderMode_ = mode;
            renderer_->SetDebugRenderMode(debugRenderMode_);
        }
    };

    if (window_.IsKeyPressed(platform::Key::Num1))
    {
        setDebugMode(renderer::DebugRenderMode::Lit);
    }
    else if (window_.IsKeyPressed(platform::Key::Num2))
    {
        setDebugMode(renderer::DebugRenderMode::Albedo);
    }
    else if (window_.IsKeyPressed(platform::Key::Num3))
    {
        setDebugMode(renderer::DebugRenderMode::Normal);
    }
    else if (window_.IsKeyPressed(platform::Key::Num4))
    {
        setDebugMode(renderer::DebugRenderMode::Roughness);
    }
    else if (window_.IsKeyPressed(platform::Key::Num5))
    {
        setDebugMode(renderer::DebugRenderMode::Metallic);
    }
    else if (window_.IsKeyPressed(platform::Key::Num6))
    {
        setDebugMode(renderer::DebugRenderMode::Shadow);
    }

    cameraController_.Update(window_, scene_.GetCamera(), deltaSeconds);
    scene_.Update(deltaSeconds);
}
}
