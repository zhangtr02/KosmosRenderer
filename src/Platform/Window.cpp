#include "Platform/Window.h"

#include <GLFW/glfw3.h>

#include <stdexcept>

namespace kosmos::platform
{
namespace
{
int glfwWindowCount = 0;
}

Window::~Window()
{
    Shutdown();
}

void Window::Initialize(int width, int height, const std::string& title)
{
    if (handle_ != nullptr)
    {
        return;
    }

    if (glfwWindowCount == 0 && glfwInit() != GLFW_TRUE)
    {
        throw std::runtime_error("Failed to initialize GLFW.");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    handle_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (handle_ == nullptr)
    {
        if (glfwWindowCount == 0)
        {
            glfwTerminate();
        }
        throw std::runtime_error("Failed to create GLFW window.");
    }

    ++glfwWindowCount;
    glfwSetWindowUserPointer(handle_, this);
    glfwSetFramebufferSizeCallback(handle_, FramebufferResizeCallback);
}

void Window::Shutdown()
{
    if (handle_ == nullptr)
    {
        return;
    }

    glfwDestroyWindow(handle_);
    handle_ = nullptr;
    --glfwWindowCount;

    if (glfwWindowCount == 0)
    {
        glfwTerminate();
    }
}

bool Window::ShouldClose() const
{
    return glfwWindowShouldClose(handle_) == GLFW_TRUE;
}

void Window::RequestClose()
{
    glfwSetWindowShouldClose(handle_, GLFW_TRUE);
}

void Window::PollEvents()
{
    glfwPollEvents();
}

void Window::WaitEvents()
{
    glfwWaitEvents();
}

bool Window::ConsumeFramebufferResize()
{
    const bool resized = framebufferResized_;
    framebufferResized_ = false;
    return resized;
}

Extent Window::GetFramebufferSize() const
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(handle_, &width, &height);
    return Extent{static_cast<unsigned int>(width), static_cast<unsigned int>(height)};
}

bool Window::IsKeyPressed(Key key) const
{
    return glfwGetKey(handle_, ToGlfwKey(key)) == GLFW_PRESS;
}

bool Window::IsMouseButtonPressed(MouseButton button) const
{
    return glfwGetMouseButton(handle_, ToGlfwMouseButton(button)) == GLFW_PRESS;
}

CursorPosition Window::GetCursorPosition() const
{
    CursorPosition position{};
    glfwGetCursorPos(handle_, &position.x, &position.y);
    return position;
}

void Window::SetCursorCaptured(bool captured)
{
    glfwSetInputMode(handle_, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

std::vector<const char*> Window::GetRequiredVulkanExtensions() const
{
    unsigned int extensionCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (extensions == nullptr || extensionCount == 0)
    {
        throw std::runtime_error("GLFW did not report Vulkan instance extensions.");
    }

    return std::vector<const char*>(extensions, extensions + extensionCount);
}

VkSurfaceKHR Window::CreateVulkanSurface(VkInstance instance) const
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(instance, handle_, nullptr, &surface) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create Vulkan window surface.");
    }
    return surface;
}

void Window::FramebufferResizeCallback(GLFWwindow* window, int, int)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr)
    {
        self->framebufferResized_ = true;
    }
}

int Window::ToGlfwKey(Key key)
{
    switch (key)
    {
    case Key::W:
        return GLFW_KEY_W;
    case Key::A:
        return GLFW_KEY_A;
    case Key::S:
        return GLFW_KEY_S;
    case Key::D:
        return GLFW_KEY_D;
    case Key::Q:
        return GLFW_KEY_Q;
    case Key::E:
        return GLFW_KEY_E;
    case Key::Escape:
        return GLFW_KEY_ESCAPE;
    }

    return GLFW_KEY_UNKNOWN;
}

int Window::ToGlfwMouseButton(MouseButton button)
{
    switch (button)
    {
    case MouseButton::Right:
        return GLFW_MOUSE_BUTTON_RIGHT;
    }

    return GLFW_MOUSE_BUTTON_1;
}
}
