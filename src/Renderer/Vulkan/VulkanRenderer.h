#pragma once

#include "Renderer/Renderer.h"

#include <array>
#include <cstddef>
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

    struct GpuMesh
    {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;
        std::uint32_t indexCount = 0;
        std::size_t materialIndex = 0;
    };

    struct GpuTexture
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory imageMemory = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    struct GpuMaterial
    {
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };

    void CreateInstance();
    void SetupDebugMessenger();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateSwapchain();
    void CreateImageViews();
    void CreateDepthResources();
    void CreateDescriptorSetLayout();
    void CreateGraphicsPipeline();
    void CreateCommandPool();
    void CreateCommandBuffers();
    void CreateSyncObjects();
    void CreateSwapchainSyncObjects();

    void CleanupGraphicsPipeline();
    void CleanupSceneResources();
    void CleanupDescriptorSetLayout();
    void CleanupDepthResources();
    void CleanupSwapchainSyncObjects();
    void CleanupSwapchain();
    void RecreateSwapchain();
    void UploadSceneResources(const scene::Scene& scene);
    void RecordScenePass(VkCommandBuffer commandBuffer,
                         std::uint32_t imageIndex,
                         const scene::Scene& scene,
                         VkClearColorValue clearColor);
    void TransitionSwapchainImageLayout(VkCommandBuffer commandBuffer, std::uint32_t imageIndex, VkImageLayout newLayout);
    void TransitionDepthImageLayout(VkCommandBuffer commandBuffer, VkImageLayout newLayout);
    void TransitionImageLayout(VkCommandBuffer commandBuffer,
                               VkImage image,
                               VkImageLayout oldLayout,
                               VkImageLayout newLayout,
                               VkImageAspectFlags aspectMask) const;
    VkCommandBuffer BeginSingleTimeCommands() const;
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer) const;
    void CopyBuffer(VkBuffer sourceBuffer, VkBuffer destinationBuffer, VkDeviceSize size) const;
    void CopyBufferToImage(VkBuffer sourceBuffer, VkImage destinationImage, std::uint32_t width, std::uint32_t height) const;

    VkShaderModule CreateShaderModule(const std::vector<std::uint32_t>& code) const;
    void CreateBuffer(VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer& buffer,
                      VkDeviceMemory& bufferMemory) const;
    void CreateImage(std::uint32_t width,
                     std::uint32_t height,
                     VkFormat format,
                     VkImageTiling tiling,
                     VkImageUsageFlags usage,
                     VkMemoryPropertyFlags properties,
                     VkImage& image,
                     VkDeviceMemory& imageMemory) const;
    VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) const;
    std::uint32_t FindMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

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
    std::vector<VkImageLayout> swapchainImageLayouts_;
    VkFormat swapchainImageFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    VkDescriptorSetLayout materialDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;

    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
    VkImageLayout depthImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<GpuMesh> gpuMeshes_;
    std::vector<GpuTexture> gpuTextures_;
    std::vector<GpuMaterial> gpuMaterials_;
    const scene::Scene* uploadedScene_ = nullptr;
    std::size_t uploadedSceneResourceVersion_ = static_cast<std::size_t>(-1);

    static constexpr std::size_t MaxFramesInFlight = 2;
    std::array<VkSemaphore, MaxFramesInFlight> imageAvailableSemaphores_{};
    std::array<VkFence, MaxFramesInFlight> inFlightFences_{};
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> imagesInFlight_;
    std::size_t currentFrame_ = 0;
    std::uint32_t currentImageIndex_ = 0;
};
}
