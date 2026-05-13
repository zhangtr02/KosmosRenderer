#include "App/Application.h"

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
    scene_ = scene::Scene::CreateDemoScene();

    renderer::RendererConfig rendererConfig;
    rendererConfig.applicationName = config_.title;
    rendererConfig.enableValidation = KOSMOS_ENABLE_VALIDATION != 0;
    rendererConfig.initialWidth = static_cast<unsigned int>(config_.width);
    rendererConfig.initialHeight = static_cast<unsigned int>(config_.height);

    renderer_ = renderer::CreateVulkanRenderer();
    renderer_->Initialize(window_, rendererConfig);

    std::cout << "KosmosRenderer started. Hold RMB to fly, WASD to move, Q/E to descend/ascend.\n";
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

    cameraController_.Update(window_, scene_.GetCamera(), deltaSeconds);
    scene_.Update(deltaSeconds);
}
}
