#pragma once

#include <string>
#include <vector>

#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace kosmos::platform
{
enum class Key
{
    W,
    A,
    S,
    D,
    Q,
    E,
    Escape
};

enum class MouseButton
{
    Right
};

struct Extent
{
    unsigned int width = 0;
    unsigned int height = 0;
};

struct CursorPosition
{
    double x = 0.0;
    double y = 0.0;
};

class Window
{
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void Initialize(int width, int height, const std::string& title);
    void Shutdown();

    bool ShouldClose() const;
    void RequestClose();
    void PollEvents();
    void WaitEvents();

    bool ConsumeFramebufferResize();
    Extent GetFramebufferSize() const;
    GLFWwindow* GetHandle() const { return handle_; }

    bool IsKeyPressed(Key key) const;
    bool IsMouseButtonPressed(MouseButton button) const;
    CursorPosition GetCursorPosition() const;
    void SetCursorCaptured(bool captured);

    std::vector<const char*> GetRequiredVulkanExtensions() const;
    VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const;

private:
    static void FramebufferResizeCallback(GLFWwindow* window, int width, int height);
    static int ToGlfwKey(Key key);
    static int ToGlfwMouseButton(MouseButton button);

    GLFWwindow* handle_ = nullptr;
    bool framebufferResized_ = false;
};
}
