#pragma once

#include "Renderer/Renderer.h"

#include <array>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

namespace kosmos::renderer
{
class VulkanRenderer final : public Renderer
{
public:
    VulkanRenderer() = default;
    ~VulkanRenderer() override;

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    void Initialize(platform::Window& window, const RendererConfig& config) override;
    void Resize(unsigned int width, unsigned int height) override;
    bool BeginFrame() override;
    void RenderScene(const scene::Scene& scene) override;
    void EndFrame() override;
    void WaitIdle() override;
    void Shutdown() override;

private:
    struct QueueFamilyIndices
    {
        std::uint32_t graphicsFamily = 0;
        std::uint32_t presentFamily = 0;
        bool hasGraphicsFamily = false;
        bool hasPresentFamily = false;

        bool IsComplete() const { return hasGraphicsFamily && hasPresentFamily; }
    };

    struct SwapchainSupportDetails
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    void CreateInstance();
    void SetupDebugMessenger();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateSwapchain();
    void CreateImageViews();
    void CreateRenderPass();
    void CreateFramebuffers();
    void CreateCommandPool();
    void CreateCommandBuffers();
    void CreateSyncObjects();
    void CreateSwapchainSyncObjects();

    void CleanupSwapchainSyncObjects();
    void CleanupSwapchain();
    void RecreateSwapchain();
    void RecordClearPass(VkCommandBuffer commandBuffer, std::uint32_t imageIndex, VkClearColorValue clearColor);

    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
    SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice device) const;
    bool IsDeviceSuitable(VkPhysicalDevice device) const;
    bool CheckDeviceExtensionSupport(VkPhysicalDevice device) const;

    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes) const;
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;

    platform::Window* window_ = nullptr;
    RendererConfig config_{};
    bool initialized_ = false;
    bool validationEnabled_ = false;
    bool frameStarted_ = false;
    bool clearRecorded_ = false;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    std::vector<VkFramebuffer> swapchainFramebuffers_;
    VkFormat swapchainImageFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    VkRenderPass renderPass_ = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    static constexpr std::size_t MaxFramesInFlight = 2;
    std::array<VkSemaphore, MaxFramesInFlight> imageAvailableSemaphores_{};
    std::array<VkFence, MaxFramesInFlight> inFlightFences_{};
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> imagesInFlight_;
    std::size_t currentFrame_ = 0;
    std::uint32_t currentImageIndex_ = 0;
};
}
