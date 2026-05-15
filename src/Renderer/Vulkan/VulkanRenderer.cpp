#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS

#include "Renderer/Vulkan/VulkanRenderer.h"

#include "Platform/Window.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

namespace kosmos::renderer
{
namespace
{
constexpr std::array<const char*, 1> ValidationLayers = {
    "VK_LAYER_KHRONOS_validation",
};

constexpr std::array<const char*, 1> DeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

#ifndef KOSMOS_SHADER_DIRECTORY
#define KOSMOS_SHADER_DIRECTORY "shaders"
#endif

struct PushConstants
{
    glm::mat4 mvp{1.0f};
    glm::mat4 model{1.0f};
    glm::mat4 lightMvp{1.0f};
    glm::vec4 baseColor{1.0f};
    glm::vec4 lightDirectionIntensity{0.0f, -1.0f, 0.0f, 1.0f};
    glm::vec4 cameraPositionMetallic{0.0f};
    glm::vec4 lightColorRoughness{1.0f};
};

static_assert(sizeof(PushConstants) <= 256, "Push constants should stay within the common Vulkan 256-byte limit.");

struct PostProcessPushConstants
{
    glm::vec4 postParams{1.0f, 2.2f, 0.0f, 0.0f};
};

void Check(VkResult result, const char* message)
{
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error(message);
    }
}

void CheckImGuiVkResult(VkResult result)
{
    if (result != VK_SUCCESS)
    {
        std::cerr << "Dear ImGui Vulkan error: " << result << '\n';
    }
}

std::string ShaderPath(std::string_view fileName)
{
    return std::string(KOSMOS_SHADER_DIRECTORY) + "/" + std::string(fileName);
}

std::vector<std::uint32_t> ReadSpirvFile(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        throw std::runtime_error("Failed to open shader file: " + filePath);
    }

    const auto fileSize = static_cast<std::streamsize>(file.tellg());
    if (fileSize <= 0 || fileSize % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0)
    {
        throw std::runtime_error("Shader file is empty or not valid SPIR-V: " + filePath);
    }

    std::vector<std::uint32_t> buffer(static_cast<std::size_t>(fileSize) / sizeof(std::uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    if (!file)
    {
        throw std::runtime_error("Failed to read shader file: " + filePath);
    }

    return buffer;
}

glm::mat4 BuildModelMatrix(const scene::Transform& transform)
{
    glm::mat4 model{1.0f};
    model = glm::translate(model, transform.translation);
    model = glm::rotate(model, glm::radians(transform.rotationDegrees.x), glm::vec3{1.0f, 0.0f, 0.0f});
    model = glm::rotate(model, glm::radians(transform.rotationDegrees.y), glm::vec3{0.0f, 1.0f, 0.0f});
    model = glm::rotate(model, glm::radians(transform.rotationDegrees.z), glm::vec3{0.0f, 0.0f, 1.0f});
    model = glm::scale(model, transform.scale);
    return model;
}

glm::vec4 ToVec4(const scene::Color& color)
{
    return glm::vec4{color.r, color.g, color.b, color.a};
}

const char* DebugRenderModeName(DebugRenderMode mode)
{
    switch (mode)
    {
    case DebugRenderMode::Lit:
        return "Lit";
    case DebugRenderMode::Albedo:
        return "Albedo";
    case DebugRenderMode::Normal:
        return "Normal";
    case DebugRenderMode::Roughness:
        return "Roughness";
    case DebugRenderMode::Metallic:
        return "Metallic";
    case DebugRenderMode::Shadow:
        return "Shadow";
    }

    return "Lit";
}

DebugRenderMode DebugRenderModeFromIndex(int index)
{
    switch (index)
    {
    case 1:
        return DebugRenderMode::Albedo;
    case 2:
        return DebugRenderMode::Normal;
    case 3:
        return DebugRenderMode::Roughness;
    case 4:
        return DebugRenderMode::Metallic;
    case 5:
        return DebugRenderMode::Shadow;
    default:
        return DebugRenderMode::Lit;
    }
}

glm::mat4 BuildLightViewProjection(const scene::DirectionalLight& light)
{
    const glm::vec3 lightDirection = glm::normalize(light.direction);
    const glm::vec3 target{0.0f, 0.0f, 0.0f};
    const glm::vec3 lightPosition = target - lightDirection * 14.0f;
    const glm::vec3 up = std::abs(lightDirection.y) > 0.95f
                             ? glm::vec3{0.0f, 0.0f, 1.0f}
                             : glm::vec3{0.0f, 1.0f, 0.0f};

    const glm::mat4 lightView = glm::lookAt(lightPosition, target, up);
    glm::mat4 lightProjection = glm::ortho(-12.0f, 12.0f, -12.0f, 12.0f, 0.1f, 40.0f);
    lightProjection[1][1] *= -1.0f;
    return lightProjection * lightView;
}

bool CheckValidationLayerSupport()
{
    std::uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* requiredLayer : ValidationLayers)
    {
        const bool found = std::any_of(availableLayers.begin(), availableLayers.end(), [requiredLayer](const VkLayerProperties& layer) {
            return std::strcmp(requiredLayer, layer.layerName) == 0;
        });

        if (!found)
        {
            return false;
        }
    }

    return true;
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                             void*)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        std::cerr << "Vulkan validation: " << callbackData->pMessage << '\n';
    }

    return VK_FALSE;
}

void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
{
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;
}

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance,
                                      const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
                                      const VkAllocationCallbacks* allocator,
                                      VkDebugUtilsMessengerEXT* debugMessenger)
{
    auto function = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (function == nullptr)
    {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    return function(instance, createInfo, allocator, debugMessenger);
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance,
                                   VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks* allocator)
{
    auto function = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (function != nullptr)
    {
        function(instance, debugMessenger, allocator);
    }
}
}

VulkanRenderer::~VulkanRenderer()
{
    Shutdown();
}

std::unique_ptr<Renderer> CreateVulkanRenderer()
{
    return std::make_unique<VulkanRenderer>();
}

void VulkanRenderer::Initialize(platform::Window& window, const RendererConfig& config)
{
    if (initialized_)
    {
        return;
    }

    window_ = &window;
    config_ = config;
    validationEnabled_ = config_.enableValidation && CheckValidationLayerSupport();
    if (config_.enableValidation && !validationEnabled_)
    {
        std::cerr << "Vulkan validation layer requested but not available; continuing without it.\n";
    }

    CreateInstance();
    SetupDebugMessenger();
    surface_ = window_->CreateVulkanSurface(instance_);
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateSwapchain();
    CreateImageViews();
    CreateDepthResources();
    CreateSceneColorResources();
    CreateShadowResources();
    CreateDescriptorSetLayout();
    CreatePostProcessDescriptorSetLayout();
    CreateGraphicsPipeline();
    CreateShadowPipeline();
    CreatePostProcessPipeline();
    CreatePostProcessResources();
    CreateCommandPool();
    CreateCommandBuffers();
    CreateTimingResources();
    CreateSyncObjects();
    InitializeImGui();

    initialized_ = true;
}

void VulkanRenderer::Resize(unsigned int width, unsigned int height)
{
    if (!initialized_ || width == 0 || height == 0)
    {
        return;
    }

    RecreateSwapchain();
}

bool VulkanRenderer::BeginFrame()
{
    if (!initialized_ || frameStarted_)
    {
        return false;
    }

    cpuFrameStart_ = std::chrono::steady_clock::now();

    Check(vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, std::numeric_limits<std::uint64_t>::max()),
          "Failed to wait for frame fence.");

    if (gpuTimestampsSupported_ && timestampFrameReady_[currentFrame_])
    {
        std::array<std::uint64_t, 2> timestamps{};
        const VkResult queryResult = vkGetQueryPoolResults(device_,
                                                           timestampQueryPool_,
                                                           static_cast<std::uint32_t>(currentFrame_ * 2),
                                                           2,
                                                           sizeof(std::uint64_t) * timestamps.size(),
                                                           timestamps.data(),
                                                           sizeof(std::uint64_t),
                                                           VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (queryResult == VK_SUCCESS && timestamps[1] > timestamps[0])
        {
            lastGpuFrameMs_ = static_cast<float>(static_cast<double>(timestamps[1] - timestamps[0]) *
                                                 static_cast<double>(timestampPeriod_) / 1000000.0);
        }
    }

    const VkResult acquireResult = vkAcquireNextImageKHR(device_,
                                                         swapchain_,
                                                         std::numeric_limits<std::uint64_t>::max(),
                                                         imageAvailableSemaphores_[currentFrame_],
                                                         VK_NULL_HANDLE,
                                                         &currentImageIndex_);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        RecreateSwapchain();
        return false;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
    {
        throw std::runtime_error("Failed to acquire swapchain image.");
    }

    if (imagesInFlight_[currentImageIndex_] != VK_NULL_HANDLE)
    {
        Check(vkWaitForFences(device_, 1, &imagesInFlight_[currentImageIndex_], VK_TRUE, std::numeric_limits<std::uint64_t>::max()),
              "Failed to wait for image fence.");
    }
    imagesInFlight_[currentImageIndex_] = inFlightFences_[currentFrame_];

    Check(vkResetFences(device_, 1, &inFlightFences_[currentFrame_]), "Failed to reset frame fence.");
    Check(vkResetCommandBuffer(commandBuffers_[currentFrame_], 0), "Failed to reset command buffer.");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(commandBuffers_[currentFrame_], &beginInfo), "Failed to begin command buffer.");

    if (gpuTimestampsSupported_)
    {
        const std::uint32_t queryIndex = static_cast<std::uint32_t>(currentFrame_ * 2);
        vkCmdResetQueryPool(commandBuffers_[currentFrame_], timestampQueryPool_, queryIndex, 2);
        vkCmdWriteTimestamp(commandBuffers_[currentFrame_], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampQueryPool_, queryIndex);
        timestampFrameReady_[currentFrame_] = true;
    }

    frameStarted_ = true;
    clearRecorded_ = false;
    return true;
}

void VulkanRenderer::RenderScene(const scene::Scene& scene)
{
    if (!frameStarted_)
    {
        return;
    }

    UploadSceneResources(scene);
    BuildDebugUi();
    RecordRenderGraph(scene);
    clearRecorded_ = true;
}

void VulkanRenderer::EndFrame()
{
    if (!frameStarted_)
    {
        return;
    }

    if (!clearRecorded_)
    {
        const scene::Scene fallbackScene = scene::Scene::CreateDemoScene();
        UploadSceneResources(fallbackScene);
        BuildDebugUi();
        RecordRenderGraph(fallbackScene);
    }

    if (gpuTimestampsSupported_)
    {
        vkCmdWriteTimestamp(commandBuffers_[currentFrame_],
                            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            timestampQueryPool_,
                            static_cast<std::uint32_t>(currentFrame_ * 2 + 1));
    }

    Check(vkEndCommandBuffer(commandBuffers_[currentFrame_]), "Failed to end command buffer.");

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores_[currentFrame_]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores_[currentImageIndex_]};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[currentFrame_];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    Check(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[currentFrame_]), "Failed to submit command buffer.");

    VkSwapchainKHR swapchains[] = {swapchain_};
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &currentImageIndex_;

    const VkResult presentResult = vkQueuePresentKHR(presentQueue_, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
    {
        RecreateSwapchain();
    }
    else if (presentResult != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to present swapchain image.");
    }

    currentFrame_ = (currentFrame_ + 1) % MaxFramesInFlight;
    frameStarted_ = false;
    clearRecorded_ = false;
    lastCpuFrameMs_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - cpuFrameStart_).count();
}

void VulkanRenderer::SetDebugRenderMode(DebugRenderMode mode)
{
    debugRenderMode_ = mode;
}

void VulkanRenderer::WaitIdle()
{
    if (device_ != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device_);
    }
}

void VulkanRenderer::Shutdown()
{
    if (!initialized_ && instance_ == VK_NULL_HANDLE)
    {
        return;
    }

    WaitIdle();
    ShutdownImGui();
    CleanupSwapchain();
    CleanupSceneResources();
    CleanupShadowResources();
    CleanupPostProcessDescriptorSetLayout();
    CleanupDescriptorSetLayout();
    CleanupTimingResources();

    for (std::size_t i = 0; i < MaxFramesInFlight; ++i)
    {
        if (imageAvailableSemaphores_[i] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device_, imageAvailableSemaphores_[i], nullptr);
            imageAvailableSemaphores_[i] = VK_NULL_HANDLE;
        }
        if (inFlightFences_[i] != VK_NULL_HANDLE)
        {
            vkDestroyFence(device_, inFlightFences_[i], nullptr);
            inFlightFences_[i] = VK_NULL_HANDLE;
        }
    }

    if (commandPool_ != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
    }

    if (device_ != VK_NULL_HANDLE)
    {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (surface_ != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }

    if (debugMessenger_ != VK_NULL_HANDLE)
    {
        DestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
        debugMessenger_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }

    physicalDevice_ = VK_NULL_HANDLE;
    graphicsQueue_ = VK_NULL_HANDLE;
    presentQueue_ = VK_NULL_HANDLE;
    window_ = nullptr;
    initialized_ = false;
    frameStarted_ = false;
}

void VulkanRenderer::CreateInstance()
{
    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = config_.applicationName.c_str();
    applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    applicationInfo.pEngineName = "KosmosRenderer";
    applicationInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> extensions = window_->GetRequiredVulkanExtensions();
    if (validationEnabled_)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (validationEnabled_)
    {
        createInfo.enabledLayerCount = static_cast<std::uint32_t>(ValidationLayers.size());
        createInfo.ppEnabledLayerNames = ValidationLayers.data();
        PopulateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = &debugCreateInfo;
    }

    Check(vkCreateInstance(&createInfo, nullptr, &instance_), "Failed to create Vulkan instance.");
}

void VulkanRenderer::SetupDebugMessenger()
{
    if (!validationEnabled_)
    {
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    PopulateDebugMessengerCreateInfo(createInfo);
    Check(CreateDebugUtilsMessengerEXT(instance_, &createInfo, nullptr, &debugMessenger_),
          "Failed to create Vulkan debug messenger.");
}

void VulkanRenderer::PickPhysicalDevice()
{
    std::uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    if (deviceCount == 0)
    {
        throw std::runtime_error("Failed to find GPUs with Vulkan support.");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

    for (VkPhysicalDevice device : devices)
    {
        if (IsDeviceSuitable(device))
        {
            physicalDevice_ = device;
            break;
        }
    }

    if (physicalDevice_ == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Failed to find a suitable Vulkan GPU.");
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    std::cout << "Vulkan GPU: " << properties.deviceName << '\n';
}

void VulkanRenderer::CreateLogicalDevice()
{
    const QueueFamilyIndices indices = FindQueueFamilies(physicalDevice_);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    const std::set<std::uint32_t> uniqueQueueFamilies = {indices.graphicsFamily, indices.presentFamily};

    float queuePriority = 1.0f;
    for (std::uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{};
    dynamicRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamicRenderingFeatures.dynamicRendering = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &dynamicRenderingFeatures;
    createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(DeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = DeviceExtensions.data();

    if (validationEnabled_)
    {
        createInfo.enabledLayerCount = static_cast<std::uint32_t>(ValidationLayers.size());
        createInfo.ppEnabledLayerNames = ValidationLayers.data();
    }

    Check(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_), "Failed to create Vulkan logical device.");
    vkGetDeviceQueue(device_, indices.graphicsFamily, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, indices.presentFamily, 0, &presentQueue_);
}

void VulkanRenderer::CreateSwapchain()
{
    const SwapchainSupportDetails support = QuerySwapchainSupport(physicalDevice_);
    const VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(support.formats);
    const VkPresentModeKHR presentMode = ChooseSwapPresentMode(support.presentModes);
    const VkExtent2D extent = ChooseSwapExtent(support.capabilities);

    std::uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
    {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface_;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const QueueFamilyIndices indices = FindQueueFamilies(physicalDevice_);
    std::uint32_t queueFamilyIndices[] = {indices.graphicsFamily, indices.presentFamily};
    if (indices.graphicsFamily != indices.presentFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    Check(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_), "Failed to create Vulkan swapchain.");

    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
    swapchainImages_.resize(imageCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapchainImages_.data());

    swapchainImageFormat_ = surfaceFormat.format;
    swapchainExtent_ = extent;
    swapchainImageLayouts_.assign(swapchainImages_.size(), VK_IMAGE_LAYOUT_UNDEFINED);
    imagesInFlight_.assign(swapchainImages_.size(), VK_NULL_HANDLE);
    renderFinishedSemaphores_.assign(swapchainImages_.size(), VK_NULL_HANDLE);
}

void VulkanRenderer::CreateImageViews()
{
    swapchainImageViews_.resize(swapchainImages_.size());
    for (std::size_t i = 0; i < swapchainImages_.size(); ++i)
    {
        swapchainImageViews_[i] = CreateImageView(swapchainImages_[i], swapchainImageFormat_, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

void VulkanRenderer::CreateDepthResources()
{
    CreateImage(swapchainExtent_.width,
                swapchainExtent_.height,
                depthFormat_,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                depthImage_,
                depthImageMemory_);
    depthImageView_ = CreateImageView(depthImage_, depthFormat_, VK_IMAGE_ASPECT_DEPTH_BIT);
    depthImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
}

void VulkanRenderer::CreateSceneColorResources()
{
    CreateImage(swapchainExtent_.width,
                swapchainExtent_.height,
                sceneColorFormat_,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                sceneColorImage_,
                sceneColorImageMemory_);
    sceneColorImageView_ = CreateImageView(sceneColorImage_, sceneColorFormat_, VK_IMAGE_ASPECT_COLOR_BIT);
    sceneColorImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    Check(vkCreateSampler(device_, &samplerInfo, nullptr, &sceneColorSampler_), "Failed to create scene color sampler.");
}

void VulkanRenderer::CreateShadowResources()
{
    CreateImage(shadowExtent_.width,
                shadowExtent_.height,
                depthFormat_,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                shadowImage_,
                shadowImageMemory_);
    shadowImageView_ = CreateImageView(shadowImage_, depthFormat_, VK_IMAGE_ASPECT_DEPTH_BIT);
    shadowImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    Check(vkCreateSampler(device_, &samplerInfo, nullptr, &shadowSampler_), "Failed to create shadow sampler.");
}

void VulkanRenderer::CreateDescriptorSetLayout()
{
    std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
    for (std::uint32_t binding = 0; binding < 3; ++binding)
    {
        bindings[binding].binding = binding;
        bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        bindings[binding].descriptorCount = 1;
        bindings[binding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    createInfo.pBindings = bindings.data();

    Check(vkCreateDescriptorSetLayout(device_, &createInfo, nullptr, &materialDescriptorSetLayout_),
          "Failed to create material descriptor set layout.");
}

void VulkanRenderer::CreatePostProcessDescriptorSetLayout()
{
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    createInfo.pBindings = bindings.data();

    Check(vkCreateDescriptorSetLayout(device_, &createInfo, nullptr, &postProcessDescriptorSetLayout_),
          "Failed to create post-process descriptor set layout.");
}

void VulkanRenderer::CreateGraphicsPipeline()
{
    const std::vector<std::uint32_t> vertexShaderCode = ReadSpirvFile(ShaderPath("mesh.vert.spv"));
    const std::vector<std::uint32_t> fragmentShaderCode = ReadSpirvFile(ShaderPath("mesh.frag.spv"));

    const VkShaderModule vertexShaderModule = CreateShaderModule(vertexShaderCode);
    const VkShaderModule fragmentShaderModule = CreateShaderModule(fragmentShaderCode);

    VkPipelineShaderStageCreateInfo vertexShaderStageInfo{};
    vertexShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexShaderStageInfo.module = vertexShaderModule;
    vertexShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentShaderStageInfo{};
    fragmentShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentShaderStageInfo.module = fragmentShaderModule;
    fragmentShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertexShaderStageInfo, fragmentShaderStageInfo};

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(scene::Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = static_cast<std::uint32_t>(offsetof(scene::Vertex, position));
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = static_cast<std::uint32_t>(offsetof(scene::Vertex, normal));
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = static_cast<std::uint32_t>(offsetof(scene::Vertex, texCoord));
    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[3].offset = static_cast<std::uint32_t>(offsetof(scene::Vertex, tangent));

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                          VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT |
                                          VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &materialDescriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    Check(vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_), "Failed to create pipeline layout.");

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &sceneColorFormat_;
    renderingInfo.depthAttachmentFormat = depthFormat_;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = static_cast<std::uint32_t>(std::size(shaderStages));
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    const VkResult pipelineResult = vkCreateGraphicsPipelines(device_,
                                                              VK_NULL_HANDLE,
                                                              1,
                                                              &pipelineInfo,
                                                              nullptr,
                                                              &graphicsPipeline_);

    vkDestroyShaderModule(device_, fragmentShaderModule, nullptr);
    vkDestroyShaderModule(device_, vertexShaderModule, nullptr);

    if (pipelineResult != VK_SUCCESS)
    {
        CleanupGraphicsPipeline();
    }
    Check(pipelineResult, "Failed to create graphics pipeline.");
}

void VulkanRenderer::CreateShadowPipeline()
{
    const std::vector<std::uint32_t> vertexShaderCode = ReadSpirvFile(ShaderPath("shadow.vert.spv"));
    const VkShaderModule vertexShaderModule = CreateShaderModule(vertexShaderCode);

    VkPipelineShaderStageCreateInfo vertexShaderStageInfo{};
    vertexShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexShaderStageInfo.module = vertexShaderModule;
    vertexShaderStageInfo.pName = "main";

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(scene::Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription positionAttribute{};
    positionAttribute.binding = 0;
    positionAttribute.location = 0;
    positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    positionAttribute.offset = static_cast<std::uint32_t>(offsetof(scene::Vertex, position));

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &positionAttribute;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    Check(vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &shadowPipelineLayout_),
          "Failed to create shadow pipeline layout.");

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 0;
    renderingInfo.depthAttachmentFormat = depthFormat_;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 1;
    pipelineInfo.pStages = &vertexShaderStageInfo;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = shadowPipelineLayout_;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;

    const VkResult pipelineResult = vkCreateGraphicsPipelines(device_,
                                                              VK_NULL_HANDLE,
                                                              1,
                                                              &pipelineInfo,
                                                              nullptr,
                                                              &shadowPipeline_);

    vkDestroyShaderModule(device_, vertexShaderModule, nullptr);

    if (pipelineResult != VK_SUCCESS)
    {
        CleanupGraphicsPipeline();
    }
    Check(pipelineResult, "Failed to create shadow pipeline.");
}

void VulkanRenderer::CreatePostProcessPipeline()
{
    const std::vector<std::uint32_t> vertexShaderCode = ReadSpirvFile(ShaderPath("fullscreen.vert.spv"));
    const std::vector<std::uint32_t> fragmentShaderCode = ReadSpirvFile(ShaderPath("post.frag.spv"));

    const VkShaderModule vertexShaderModule = CreateShaderModule(vertexShaderCode);
    const VkShaderModule fragmentShaderModule = CreateShaderModule(fragmentShaderCode);

    VkPipelineShaderStageCreateInfo vertexShaderStageInfo{};
    vertexShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexShaderStageInfo.module = vertexShaderModule;
    vertexShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentShaderStageInfo{};
    fragmentShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentShaderStageInfo.module = fragmentShaderModule;
    fragmentShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertexShaderStageInfo, fragmentShaderStageInfo};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                          VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT |
                                          VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PostProcessPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &postProcessDescriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    Check(vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &postProcessPipelineLayout_),
          "Failed to create post-process pipeline layout.");

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &swapchainImageFormat_;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = static_cast<std::uint32_t>(std::size(shaderStages));
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = postProcessPipelineLayout_;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    const VkResult pipelineResult = vkCreateGraphicsPipelines(device_,
                                                              VK_NULL_HANDLE,
                                                              1,
                                                              &pipelineInfo,
                                                              nullptr,
                                                              &postProcessPipeline_);

    vkDestroyShaderModule(device_, fragmentShaderModule, nullptr);
    vkDestroyShaderModule(device_, vertexShaderModule, nullptr);

    if (pipelineResult != VK_SUCCESS)
    {
        CleanupGraphicsPipeline();
    }
    Check(pipelineResult, "Failed to create post-process pipeline.");
}

void VulkanRenderer::CreatePostProcessResources()
{
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;
    Check(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &postProcessDescriptorPool_),
          "Failed to create post-process descriptor pool.");

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = postProcessDescriptorPool_;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &postProcessDescriptorSetLayout_;
    Check(vkAllocateDescriptorSets(device_, &allocateInfo, &postProcessDescriptorSet_),
          "Failed to allocate post-process descriptor set.");

    VkDescriptorImageInfo sceneColorInfo{};
    sceneColorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    sceneColorInfo.imageView = sceneColorImageView_;

    VkDescriptorImageInfo samplerInfo{};
    samplerInfo.sampler = sceneColorSampler_;

    std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = postProcessDescriptorSet_;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    descriptorWrites[0].pImageInfo = &sceneColorInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = postProcessDescriptorSet_;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    descriptorWrites[1].pImageInfo = &samplerInfo;

    vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void VulkanRenderer::CreateCommandPool()
{
    const QueueFamilyIndices queueFamilyIndices = FindQueueFamilies(physicalDevice_);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;

    Check(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_), "Failed to create command pool.");
}

void VulkanRenderer::CreateCommandBuffers()
{
    commandBuffers_.resize(MaxFramesInFlight);

    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = commandPool_;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = static_cast<std::uint32_t>(commandBuffers_.size());

    Check(vkAllocateCommandBuffers(device_, &allocateInfo, commandBuffers_.data()), "Failed to allocate command buffers.");
}

void VulkanRenderer::CreateTimingResources()
{
    VkPhysicalDeviceProperties physicalProperties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &physicalProperties);
    timestampPeriod_ = physicalProperties.limits.timestampPeriod;

    std::uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, queueFamilies.data());

    const QueueFamilyIndices indices = FindQueueFamilies(physicalDevice_);
    gpuTimestampsSupported_ = indices.graphicsFamily < queueFamilies.size() &&
                              queueFamilies[indices.graphicsFamily].timestampValidBits > 0;
    if (!gpuTimestampsSupported_)
    {
        return;
    }

    VkQueryPoolCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    createInfo.queryCount = static_cast<std::uint32_t>(MaxFramesInFlight * 2);
    Check(vkCreateQueryPool(device_, &createInfo, nullptr, &timestampQueryPool_), "Failed to create timestamp query pool.");
}

void VulkanRenderer::CreateSyncObjects()
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (std::size_t i = 0; i < MaxFramesInFlight; ++i)
    {
        Check(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]),
              "Failed to create image available semaphore.");
        Check(vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]),
              "Failed to create frame fence.");
    }

    CreateSwapchainSyncObjects();
}

void VulkanRenderer::CreateSwapchainSyncObjects()
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (VkSemaphore& semaphore : renderFinishedSemaphores_)
    {
        Check(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &semaphore),
              "Failed to create swapchain render finished semaphore.");
    }
}

void VulkanRenderer::InitializeImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForVulkan(window_->GetHandle(), true))
    {
        throw std::runtime_error("Failed to initialize Dear ImGui GLFW backend.");
    }

    const QueueFamilyIndices indices = FindQueueFamilies(physicalDevice_);
    VkPipelineRenderingCreateInfoKHR renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &swapchainImageFormat_;

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.Instance = instance_;
    initInfo.PhysicalDevice = physicalDevice_;
    initInfo.Device = device_;
    initInfo.QueueFamily = indices.graphicsFamily;
    initInfo.Queue = graphicsQueue_;
    initInfo.DescriptorPoolSize = 64;
    initInfo.MinImageCount = static_cast<std::uint32_t>(MaxFramesInFlight);
    initInfo.ImageCount = static_cast<std::uint32_t>(swapchainImages_.size());
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = renderingInfo;
    initInfo.CheckVkResultFn = CheckImGuiVkResult;

    if (!ImGui_ImplVulkan_Init(&initInfo))
    {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("Failed to initialize Dear ImGui Vulkan backend.");
    }

    shadowPreviewDescriptor_ = ImGui_ImplVulkan_AddTexture(shadowSampler_,
                                                           shadowImageView_,
                                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    imguiInitialized_ = true;
}

void VulkanRenderer::CleanupSwapchainSyncObjects()
{
    for (VkSemaphore semaphore : renderFinishedSemaphores_)
    {
        if (semaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device_, semaphore, nullptr);
        }
    }
    renderFinishedSemaphores_.clear();
}

void VulkanRenderer::CleanupSceneResources()
{
    for (GpuMesh& mesh : gpuMeshes_)
    {
        if (mesh.indexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, mesh.indexBuffer, nullptr);
            mesh.indexBuffer = VK_NULL_HANDLE;
        }
        if (mesh.indexBufferMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, mesh.indexBufferMemory, nullptr);
            mesh.indexBufferMemory = VK_NULL_HANDLE;
        }
        if (mesh.vertexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, mesh.vertexBuffer, nullptr);
            mesh.vertexBuffer = VK_NULL_HANDLE;
        }
        if (mesh.vertexBufferMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, mesh.vertexBufferMemory, nullptr);
            mesh.vertexBufferMemory = VK_NULL_HANDLE;
        }
    }
    gpuMeshes_.clear();

    for (GpuTexture& texture : gpuTextures_)
    {
        if (texture.sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(device_, texture.sampler, nullptr);
            texture.sampler = VK_NULL_HANDLE;
        }
        if (texture.imageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device_, texture.imageView, nullptr);
            texture.imageView = VK_NULL_HANDLE;
        }
        if (texture.image != VK_NULL_HANDLE)
        {
            vkDestroyImage(device_, texture.image, nullptr);
            texture.image = VK_NULL_HANDLE;
        }
        if (texture.imageMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, texture.imageMemory, nullptr);
            texture.imageMemory = VK_NULL_HANDLE;
        }
    }
    gpuTextures_.clear();
    gpuMaterials_.clear();

    if (descriptorPool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }

    uploadedScene_ = nullptr;
    uploadedSceneResourceVersion_ = static_cast<std::size_t>(-1);
}

void VulkanRenderer::CleanupDescriptorSetLayout()
{
    if (materialDescriptorSetLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device_, materialDescriptorSetLayout_, nullptr);
        materialDescriptorSetLayout_ = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::CleanupPostProcessDescriptorSetLayout()
{
    if (postProcessDescriptorSetLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device_, postProcessDescriptorSetLayout_, nullptr);
        postProcessDescriptorSetLayout_ = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::CleanupDepthResources()
{
    if (depthImageView_ != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device_, depthImageView_, nullptr);
        depthImageView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE)
    {
        vkDestroyImage(device_, depthImage_, nullptr);
        depthImage_ = VK_NULL_HANDLE;
    }
    if (depthImageMemory_ != VK_NULL_HANDLE)
    {
        vkFreeMemory(device_, depthImageMemory_, nullptr);
        depthImageMemory_ = VK_NULL_HANDLE;
    }

    depthImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
}

void VulkanRenderer::CleanupSceneColorResources()
{
    if (sceneColorSampler_ != VK_NULL_HANDLE)
    {
        vkDestroySampler(device_, sceneColorSampler_, nullptr);
        sceneColorSampler_ = VK_NULL_HANDLE;
    }
    if (sceneColorImageView_ != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device_, sceneColorImageView_, nullptr);
        sceneColorImageView_ = VK_NULL_HANDLE;
    }
    if (sceneColorImage_ != VK_NULL_HANDLE)
    {
        vkDestroyImage(device_, sceneColorImage_, nullptr);
        sceneColorImage_ = VK_NULL_HANDLE;
    }
    if (sceneColorImageMemory_ != VK_NULL_HANDLE)
    {
        vkFreeMemory(device_, sceneColorImageMemory_, nullptr);
        sceneColorImageMemory_ = VK_NULL_HANDLE;
    }

    sceneColorImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
}

void VulkanRenderer::CleanupShadowResources()
{
    if (shadowSampler_ != VK_NULL_HANDLE)
    {
        vkDestroySampler(device_, shadowSampler_, nullptr);
        shadowSampler_ = VK_NULL_HANDLE;
    }
    if (shadowImageView_ != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device_, shadowImageView_, nullptr);
        shadowImageView_ = VK_NULL_HANDLE;
    }
    if (shadowImage_ != VK_NULL_HANDLE)
    {
        vkDestroyImage(device_, shadowImage_, nullptr);
        shadowImage_ = VK_NULL_HANDLE;
    }
    if (shadowImageMemory_ != VK_NULL_HANDLE)
    {
        vkFreeMemory(device_, shadowImageMemory_, nullptr);
        shadowImageMemory_ = VK_NULL_HANDLE;
    }

    shadowImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
}

void VulkanRenderer::CleanupPostProcessResources()
{
    if (postProcessDescriptorPool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device_, postProcessDescriptorPool_, nullptr);
        postProcessDescriptorPool_ = VK_NULL_HANDLE;
    }

    postProcessDescriptorSet_ = VK_NULL_HANDLE;
}

void VulkanRenderer::CleanupTimingResources()
{
    if (timestampQueryPool_ != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(device_, timestampQueryPool_, nullptr);
        timestampQueryPool_ = VK_NULL_HANDLE;
    }

    gpuTimestampsSupported_ = false;
    timestampFrameReady_.fill(false);
    lastGpuFrameMs_ = 0.0f;
}

void VulkanRenderer::ShutdownImGui()
{
    if (!imguiInitialized_)
    {
        return;
    }

    if (shadowPreviewDescriptor_ != VK_NULL_HANDLE)
    {
        ImGui_ImplVulkan_RemoveTexture(shadowPreviewDescriptor_);
        shadowPreviewDescriptor_ = VK_NULL_HANDLE;
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    imguiInitialized_ = false;
}

void VulkanRenderer::CleanupSwapchain()
{
    CleanupSwapchainSyncObjects();
    CleanupPostProcessResources();
    CleanupGraphicsPipeline();
    CleanupSceneColorResources();
    CleanupDepthResources();

    for (VkImageView imageView : swapchainImageViews_)
    {
        if (imageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device_, imageView, nullptr);
        }
    }
    swapchainImageViews_.clear();

    if (swapchain_ != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }

    swapchainImages_.clear();
    swapchainImageLayouts_.clear();
    imagesInFlight_.clear();
}

void VulkanRenderer::RecreateSwapchain()
{
    auto size = window_->GetFramebufferSize();
    while (size.width == 0 || size.height == 0)
    {
        window_->WaitEvents();
        size = window_->GetFramebufferSize();
    }

    vkDeviceWaitIdle(device_);
    CleanupSwapchain();
    CreateSwapchain();
    CreateImageViews();
    CreateDepthResources();
    CreateSceneColorResources();
    CreateGraphicsPipeline();
    CreateShadowPipeline();
    CreatePostProcessPipeline();
    CreatePostProcessResources();
    CreateSwapchainSyncObjects();
}

void VulkanRenderer::UploadSceneResources(const scene::Scene& scene)
{
    if (uploadedScene_ == &scene && uploadedSceneResourceVersion_ == scene.GetResourceVersion())
    {
        return;
    }

    vkDeviceWaitIdle(device_);
    CleanupSceneResources();

    const auto uploadBuffer = [this](const void* sourceData,
                                     VkDeviceSize size,
                                     VkBufferUsageFlags usage,
                                     VkBuffer& destinationBuffer,
                                     VkDeviceMemory& destinationMemory) {
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
        CreateBuffer(size,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer,
                     stagingBufferMemory);

        void* mappedData = nullptr;
        Check(vkMapMemory(device_, stagingBufferMemory, 0, size, 0, &mappedData), "Failed to map staging buffer memory.");
        std::memcpy(mappedData, sourceData, static_cast<std::size_t>(size));
        vkUnmapMemory(device_, stagingBufferMemory);

        CreateBuffer(size,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     destinationBuffer,
                     destinationMemory);
        CopyBuffer(stagingBuffer, destinationBuffer, size);

        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingBufferMemory, nullptr);
    };

    const std::vector<scene::TextureData>& textures = scene.GetTextures();
    const std::vector<scene::Material>& materials = scene.GetMaterials();
    const std::uint32_t materialCount = static_cast<std::uint32_t>(std::max<std::size_t>(1, materials.size()));

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    poolSizes[0].descriptorCount = materialCount * 4;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    poolSizes[1].descriptorCount = materialCount * 2;

    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    descriptorPoolInfo.pPoolSizes = poolSizes.data();
    descriptorPoolInfo.maxSets = materialCount;
    Check(vkCreateDescriptorPool(device_, &descriptorPoolInfo, nullptr, &descriptorPool_),
          "Failed to create material descriptor pool.");

    const scene::TextureData fallbackTexture{};
    const std::size_t actualTextureCount = textures.empty() ? 1 : textures.size();
    gpuTextures_.resize(actualTextureCount);
    for (std::size_t textureIndex = 0; textureIndex < actualTextureCount; ++textureIndex)
    {
        const scene::TextureData& textureData = textures.empty() ? fallbackTexture : textures[textureIndex];
        const std::uint32_t width = std::max(1u, textureData.width);
        const std::uint32_t height = std::max(1u, textureData.height);
        const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(imageSize), 255);
        if (!textureData.rgbaPixels.empty())
        {
            std::memcpy(pixels.data(),
                        textureData.rgbaPixels.data(),
                        std::min(pixels.size(), textureData.rgbaPixels.size()));
        }

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
        CreateBuffer(imageSize,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer,
                     stagingBufferMemory);

        void* mappedData = nullptr;
        Check(vkMapMemory(device_, stagingBufferMemory, 0, imageSize, 0, &mappedData), "Failed to map texture staging memory.");
        std::memcpy(mappedData, pixels.data(), pixels.size());
        vkUnmapMemory(device_, stagingBufferMemory);

        GpuTexture& texture = gpuTextures_[textureIndex];
        const VkFormat textureFormat = textureData.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        CreateImage(width,
                    height,
                    textureFormat,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    texture.image,
                    texture.imageMemory);

        VkCommandBuffer commandBuffer = BeginSingleTimeCommands();
        TransitionImageLayout(commandBuffer,
                              texture.image,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_ASPECT_COLOR_BIT);
        EndSingleTimeCommands(commandBuffer);

        CopyBufferToImage(stagingBuffer, texture.image, width, height);

        commandBuffer = BeginSingleTimeCommands();
        TransitionImageLayout(commandBuffer,
                              texture.image,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_IMAGE_ASPECT_COLOR_BIT);
        EndSingleTimeCommands(commandBuffer);

        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingBufferMemory, nullptr);

        texture.imageView = CreateImageView(texture.image, textureFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        Check(vkCreateSampler(device_, &samplerInfo, nullptr, &texture.sampler), "Failed to create texture sampler.");

    }

    const auto validTextureIndex = [this](std::size_t textureIndex) {
        return textureIndex < gpuTextures_.size() ? textureIndex : std::size_t{0};
    };

    gpuMaterials_.resize(materialCount);
    for (std::size_t materialIndex = 0; materialIndex < gpuMaterials_.size(); ++materialIndex)
    {
        const scene::Material fallbackMaterial{};
        const scene::Material& material = materialIndex < materials.size() ? materials[materialIndex] : fallbackMaterial;
        const std::size_t baseColorTexture = validTextureIndex(material.baseColorTextureIndex);
        const std::size_t metallicRoughnessTexture = validTextureIndex(material.metallicRoughnessTextureIndex);
        const std::size_t normalTexture = validTextureIndex(material.normalTextureIndex);

        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = descriptorPool_;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &materialDescriptorSetLayout_;
        Check(vkAllocateDescriptorSets(device_, &allocateInfo, &gpuMaterials_[materialIndex].descriptorSet),
              "Failed to allocate material descriptor set.");

        std::array<VkDescriptorImageInfo, 4> sampledImageInfos{};
        sampledImageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sampledImageInfos[0].imageView = gpuTextures_[baseColorTexture].imageView;
        sampledImageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sampledImageInfos[1].imageView = gpuTextures_[metallicRoughnessTexture].imageView;
        sampledImageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sampledImageInfos[2].imageView = gpuTextures_[normalTexture].imageView;
        sampledImageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sampledImageInfos[3].imageView = shadowImageView_;

        std::array<VkDescriptorImageInfo, 2> samplerDescriptorInfos{};
        samplerDescriptorInfos[0].sampler = gpuTextures_[baseColorTexture].sampler;
        samplerDescriptorInfos[1].sampler = shadowSampler_;

        std::array<VkWriteDescriptorSet, 6> descriptorWrites{};
        for (std::uint32_t binding = 0; binding < 3; ++binding)
        {
            descriptorWrites[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[binding].dstSet = gpuMaterials_[materialIndex].descriptorSet;
            descriptorWrites[binding].dstBinding = binding;
            descriptorWrites[binding].descriptorCount = 1;
            descriptorWrites[binding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            descriptorWrites[binding].pImageInfo = &sampledImageInfos[binding];
        }

        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = gpuMaterials_[materialIndex].descriptorSet;
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        descriptorWrites[3].pImageInfo = &samplerDescriptorInfos[0];

        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].dstSet = gpuMaterials_[materialIndex].descriptorSet;
        descriptorWrites[4].dstBinding = 4;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        descriptorWrites[4].pImageInfo = &sampledImageInfos[3];

        descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[5].dstSet = gpuMaterials_[materialIndex].descriptorSet;
        descriptorWrites[5].dstBinding = 5;
        descriptorWrites[5].descriptorCount = 1;
        descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        descriptorWrites[5].pImageInfo = &samplerDescriptorInfos[1];

        vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }

    const std::vector<scene::Mesh>& meshes = scene.GetMeshes();
    gpuMeshes_.resize(meshes.size());
    for (std::size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
    {
        const scene::Mesh& mesh = meshes[meshIndex];
        if (mesh.vertices.empty() || mesh.indices.empty())
        {
            continue;
        }

        GpuMesh& gpuMesh = gpuMeshes_[meshIndex];
        gpuMesh.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
        gpuMesh.materialIndex = mesh.materialIndex;

        const VkDeviceSize vertexBufferSize = sizeof(scene::Vertex) * mesh.vertices.size();
        uploadBuffer(mesh.vertices.data(), vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, gpuMesh.vertexBuffer, gpuMesh.vertexBufferMemory);

        const VkDeviceSize indexBufferSize = sizeof(std::uint32_t) * mesh.indices.size();
        uploadBuffer(mesh.indices.data(), indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, gpuMesh.indexBuffer, gpuMesh.indexBufferMemory);
    }

    uploadedScene_ = &scene;
    uploadedSceneResourceVersion_ = scene.GetResourceVersion();
}

VulkanRenderer::FrameRenderContext VulkanRenderer::BuildFrameRenderContext(const scene::Scene& scene) const
{
    const auto clearColor = scene.GetClearColor();
    VkClearColorValue vkClearColor{};
    vkClearColor.float32[0] = clearColor.r;
    vkClearColor.float32[1] = clearColor.g;
    vkClearColor.float32[2] = clearColor.b;
    vkClearColor.float32[3] = clearColor.a;

    FrameRenderContext context{};
    context.commandBuffer = commandBuffers_[currentFrame_];
    context.swapchainImageIndex = currentImageIndex_;
    context.clearColor = vkClearColor;
    context.lightViewProjection = BuildLightViewProjection(scene.GetDirectionalLight());
    return context;
}

void VulkanRenderer::RecordRenderGraph(const scene::Scene& scene)
{
    const FrameRenderContext context = BuildFrameRenderContext(scene);

    // Keep the pass order in one place so a real render graph can replace it later.
    RecordShadowPass(context, scene);
    RecordForwardPass(context, scene);
    RecordPostProcessPass(context);
}

void VulkanRenderer::RecordShadowPass(const FrameRenderContext& context, const scene::Scene& scene)
{
    VkCommandBuffer commandBuffer = context.commandBuffer;
    const glm::mat4& lightViewProjection = context.lightViewProjection;

    TransitionShadowImageLayout(commandBuffer, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = shadowImageView_;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = shadowExtent_;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 0;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(shadowExtent_.width);
    viewport.height = static_cast<float>(shadowExtent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = shadowExtent_;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    for (const scene::MeshInstance& meshInstance : scene.GetMeshInstances())
    {
        if (meshInstance.meshIndex >= gpuMeshes_.size())
        {
            continue;
        }

        const GpuMesh& mesh = gpuMeshes_[meshInstance.meshIndex];
        if (mesh.indexCount == 0 || mesh.vertexBuffer == VK_NULL_HANDLE || mesh.indexBuffer == VK_NULL_HANDLE)
        {
            continue;
        }

        VkBuffer vertexBuffers[] = {mesh.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        const glm::mat4 instanceModel = meshInstance.useWorldTransform
                                            ? meshInstance.worldTransform
                                            : BuildModelMatrix(meshInstance.transform);
        const PushConstants pushConstants{
            lightViewProjection * instanceModel,
            instanceModel,
            lightViewProjection * instanceModel,
        };
        vkCmdPushConstants(commandBuffer,
                           shadowPipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT,
                           0,
                           sizeof(PushConstants),
                           &pushConstants);
        vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
    }

    vkCmdEndRendering(commandBuffer);
    TransitionShadowImageLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void VulkanRenderer::RecordForwardPass(const FrameRenderContext& context, const scene::Scene& scene)
{
    VkCommandBuffer commandBuffer = context.commandBuffer;
    const VkClearColorValue clearColor = context.clearColor;
    const glm::mat4& lightViewProjection = context.lightViewProjection;

    VkClearValue clearValue{};
    clearValue.color = clearColor;

    TransitionSceneColorImageLayout(commandBuffer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    TransitionDepthImageLayout(commandBuffer, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = sceneColorImageView_;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue = clearValue;

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depthImageView_;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = swapchainExtent_;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);

    const scene::Camera& camera = scene.GetCamera();
    const float aspectRatio = static_cast<float>(swapchainExtent_.width) / static_cast<float>(swapchainExtent_.height);
    const glm::mat4 view = glm::lookAt(camera.GetPosition(),
                                       camera.GetPosition() + camera.Forward(),
                                       glm::vec3{0.0f, 1.0f, 0.0f});
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), aspectRatio, 0.1f, 100.0f);
    projection[1][1] *= -1.0f;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchainExtent_.width);
    viewport.height = static_cast<float>(swapchainExtent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent_;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    const glm::vec3 lightDirection = glm::normalize(scene.GetDirectionalLight().direction);
    const glm::vec4 lightDirectionIntensity{lightDirection, scene.GetDirectionalLight().intensity};
    const std::vector<scene::Material>& materials = scene.GetMaterials();

    for (const scene::MeshInstance& meshInstance : scene.GetMeshInstances())
    {
        if (meshInstance.meshIndex >= gpuMeshes_.size())
        {
            continue;
        }

        const GpuMesh& mesh = gpuMeshes_[meshInstance.meshIndex];
        if (mesh.indexCount == 0 || mesh.vertexBuffer == VK_NULL_HANDLE || mesh.indexBuffer == VK_NULL_HANDLE)
        {
            continue;
        }

        const scene::Material* material = nullptr;
        if (mesh.materialIndex < materials.size())
        {
            material = &materials[mesh.materialIndex];
        }
        const glm::vec4 baseColor = material != nullptr ? ToVec4(material->baseColor) : glm::vec4{1.0f};
        const float metallic = material != nullptr ? material->metallic : 0.0f;
        const float roughness = material != nullptr ? material->roughness : 0.5f;
        const std::size_t materialIndex = mesh.materialIndex < gpuMaterials_.size() ? mesh.materialIndex : 0;
        if (materialIndex >= gpuMaterials_.size())
        {
            continue;
        }

        vkCmdBindDescriptorSets(commandBuffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout_,
                                0,
                                1,
                                &gpuMaterials_[materialIndex].descriptorSet,
                                0,
                                nullptr);

        VkBuffer vertexBuffers[] = {mesh.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        const glm::mat4 instanceModel = meshInstance.useWorldTransform
                                            ? meshInstance.worldTransform
                                            : BuildModelMatrix(meshInstance.transform);
        const scene::Color& lightColor = scene.GetDirectionalLight().color;
        const float encodedRoughness = roughness + static_cast<float>(debugRenderMode_) * 2.0f;
        const PushConstants pushConstants{
            projection * view * instanceModel,
            instanceModel,
            lightViewProjection * instanceModel,
            baseColor,
            lightDirectionIntensity,
            glm::vec4{camera.GetPosition(), metallic},
            glm::vec4{lightColor.r, lightColor.g, lightColor.b, encodedRoughness},
        };
        vkCmdPushConstants(commandBuffer,
                           pipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           sizeof(PushConstants),
                           &pushConstants);
        vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
    }
    vkCmdEndRendering(commandBuffer);
    TransitionSceneColorImageLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void VulkanRenderer::RecordPostProcessPass(const FrameRenderContext& context)
{
    VkCommandBuffer commandBuffer = context.commandBuffer;
    const std::uint32_t imageIndex = context.swapchainImageIndex;

    TransitionSwapchainImageLayout(commandBuffer, imageIndex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkClearValue clearValue{};
    clearValue.color.float32[0] = 0.0f;
    clearValue.color.float32[1] = 0.0f;
    clearValue.color.float32[2] = 0.0f;
    clearValue.color.float32[3] = 1.0f;

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = swapchainImageViews_[imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue = clearValue;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = swapchainExtent_;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, postProcessPipeline_);
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            postProcessPipelineLayout_,
                            0,
                            1,
                            &postProcessDescriptorSet_,
                            0,
                            nullptr);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchainExtent_.width);
    viewport.height = static_cast<float>(swapchainExtent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent_;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    const PostProcessPushConstants pushConstants{
        glm::vec4{exposure_, gamma_, 0.0f, 0.0f},
    };
    vkCmdPushConstants(commandBuffer,
                       postProcessPipelineLayout_,
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(PostProcessPushConstants),
                       &pushConstants);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    RecordUiPass(commandBuffer);

    vkCmdEndRendering(commandBuffer);
    TransitionSwapchainImageLayout(commandBuffer, imageIndex, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

void VulkanRenderer::RecordUiPass(VkCommandBuffer commandBuffer)
{
    if (imguiInitialized_)
    {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    }
}

void VulkanRenderer::BuildDebugUi()
{
    if (!imguiInitialized_)
    {
        return;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2{12.0f, 12.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2{340.0f, 460.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Kosmos Renderer");

    ImGui::Text("Frame Timing");
    ImGui::Text("CPU: %.2f ms", lastCpuFrameMs_);
    if (gpuTimestampsSupported_)
    {
        ImGui::Text("GPU: %.2f ms", lastGpuFrameMs_);
    }
    else
    {
        ImGui::TextUnformatted("GPU: unavailable");
    }

    ImGui::Separator();
    static constexpr const char* DebugModeNames[] = {"Lit", "Albedo", "Normal", "Roughness", "Metallic", "Shadow"};
    int debugModeIndex = static_cast<int>(debugRenderMode_);
    if (ImGui::Combo("Render Mode", &debugModeIndex, DebugModeNames, static_cast<int>(std::size(DebugModeNames))))
    {
        debugRenderMode_ = DebugRenderModeFromIndex(debugModeIndex);
    }

    ImGui::SliderFloat("Exposure", &exposure_, 0.1f, 5.0f, "%.2f");
    ImGui::SliderFloat("Gamma", &gamma_, 1.0f, 3.0f, "%.2f");

    ImGui::Separator();
    ImGui::TextUnformatted("Shadow Map");
    if (shadowPreviewDescriptor_ != VK_NULL_HANDLE)
    {
        const ImTextureID textureId = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(shadowPreviewDescriptor_));
        ImGui::Image(ImTextureRef{textureId},
                     ImVec2{220.0f, 220.0f},
                     ImVec2{0.0f, 1.0f},
                     ImVec2{1.0f, 0.0f});
    }

    ImGui::End();
    ImGui::Render();
}

void VulkanRenderer::TransitionSwapchainImageLayout(VkCommandBuffer commandBuffer,
                                                    std::uint32_t imageIndex,
                                                    VkImageLayout newLayout)
{
    VkImageLayout& currentLayout = swapchainImageLayouts_[imageIndex];
    if (currentLayout == newLayout)
    {
        return;
    }

    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkAccessFlags sourceAccess = 0;
    VkAccessFlags destinationAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    if (currentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
    {
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }

    if (newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    {
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        sourceAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        destinationAccess = 0;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    barrier.oldLayout = currentLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapchainImages_[imageIndex];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(commandBuffer,
                         sourceStage,
                         destinationStage,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);

    currentLayout = newLayout;
}

void VulkanRenderer::TransitionDepthImageLayout(VkCommandBuffer commandBuffer, VkImageLayout newLayout)
{
    if (depthImageLayout_ == newLayout)
    {
        return;
    }

    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    VkAccessFlags sourceAccess = 0;
    VkAccessFlags destinationAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    if (depthImageLayout_ != VK_IMAGE_LAYOUT_UNDEFINED)
    {
        sourceStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        sourceAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    barrier.oldLayout = depthImageLayout_;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = depthImage_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(commandBuffer,
                         sourceStage,
                         destinationStage,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);

    depthImageLayout_ = newLayout;
}

void VulkanRenderer::TransitionSceneColorImageLayout(VkCommandBuffer commandBuffer, VkImageLayout newLayout)
{
    if (sceneColorImageLayout_ == newLayout)
    {
        return;
    }

    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkAccessFlags sourceAccess = 0;
    VkAccessFlags destinationAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    if (sceneColorImageLayout_ == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        sourceAccess = VK_ACCESS_SHADER_READ_BIT;
    }
    else if (sceneColorImageLayout_ == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        sourceAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }

    if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        sourceAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        destinationAccess = VK_ACCESS_SHADER_READ_BIT;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    barrier.oldLayout = sceneColorImageLayout_;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = sceneColorImage_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(commandBuffer,
                         sourceStage,
                         destinationStage,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);

    sceneColorImageLayout_ = newLayout;
}

void VulkanRenderer::TransitionShadowImageLayout(VkCommandBuffer commandBuffer, VkImageLayout newLayout)
{
    if (shadowImageLayout_ == newLayout)
    {
        return;
    }

    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    VkAccessFlags sourceAccess = 0;
    VkAccessFlags destinationAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    if (shadowImageLayout_ == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        sourceAccess = VK_ACCESS_SHADER_READ_BIT;
    }

    if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        sourceStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        sourceAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        destinationAccess = VK_ACCESS_SHADER_READ_BIT;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    barrier.oldLayout = shadowImageLayout_;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = shadowImage_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(commandBuffer,
                         sourceStage,
                         destinationStage,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);

    shadowImageLayout_ = newLayout;
}

void VulkanRenderer::TransitionImageLayout(VkCommandBuffer commandBuffer,
                                           VkImage image,
                                           VkImageLayout oldLayout,
                                           VkImageLayout newLayout,
                                           VkImageAspectFlags aspectMask) const
{
    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkAccessFlags sourceAccess = 0;
    VkAccessFlags destinationAccess = VK_ACCESS_TRANSFER_WRITE_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
        newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        sourceAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
        destinationAccess = VK_ACCESS_SHADER_READ_BIT;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(commandBuffer,
                         sourceStage,
                         destinationStage,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);
}

VkCommandBuffer VulkanRenderer::BeginSingleTimeCommands() const
{
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandPool = commandPool_;
    allocateInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    Check(vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer), "Failed to allocate single-time command buffer.");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin single-time command buffer.");

    return commandBuffer;
}

void VulkanRenderer::EndSingleTimeCommands(VkCommandBuffer commandBuffer) const
{
    Check(vkEndCommandBuffer(commandBuffer), "Failed to end single-time command buffer.");

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    Check(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit single-time command buffer.");
    Check(vkQueueWaitIdle(graphicsQueue_), "Failed to wait for single-time command buffer.");

    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
}

void VulkanRenderer::CopyBuffer(VkBuffer sourceBuffer, VkBuffer destinationBuffer, VkDeviceSize size) const
{
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, sourceBuffer, destinationBuffer, 1, &copyRegion);

    EndSingleTimeCommands(commandBuffer);
}

void VulkanRenderer::CopyBufferToImage(VkBuffer sourceBuffer, VkImage destinationImage, std::uint32_t width, std::uint32_t height) const
{
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer,
                           sourceBuffer,
                           destinationImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &region);

    EndSingleTimeCommands(commandBuffer);
}

void VulkanRenderer::CleanupGraphicsPipeline()
{
    if (postProcessPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device_, postProcessPipeline_, nullptr);
        postProcessPipeline_ = VK_NULL_HANDLE;
    }

    if (postProcessPipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, postProcessPipelineLayout_, nullptr);
        postProcessPipelineLayout_ = VK_NULL_HANDLE;
    }

    if (shadowPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device_, shadowPipeline_, nullptr);
        shadowPipeline_ = VK_NULL_HANDLE;
    }

    if (shadowPipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, shadowPipelineLayout_, nullptr);
        shadowPipelineLayout_ = VK_NULL_HANDLE;
    }

    if (graphicsPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
        graphicsPipeline_ = VK_NULL_HANDLE;
    }

    if (pipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
}

VkShaderModule VulkanRenderer::CreateShaderModule(const std::vector<std::uint32_t>& code) const
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(std::uint32_t);
    createInfo.pCode = code.data();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    Check(vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule), "Failed to create shader module.");
    return shaderModule;
}

void VulkanRenderer::CreateBuffer(VkDeviceSize size,
                                  VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags properties,
                                  VkBuffer& buffer,
                                  VkDeviceMemory& bufferMemory) const
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    Check(vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer), "Failed to create buffer.");

    VkMemoryRequirements memoryRequirements{};
    vkGetBufferMemoryRequirements(device_, buffer, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = FindMemoryType(memoryRequirements.memoryTypeBits, properties);

    Check(vkAllocateMemory(device_, &allocateInfo, nullptr, &bufferMemory), "Failed to allocate buffer memory.");
    Check(vkBindBufferMemory(device_, buffer, bufferMemory, 0), "Failed to bind buffer memory.");
}

void VulkanRenderer::CreateImage(std::uint32_t width,
                                 std::uint32_t height,
                                 VkFormat format,
                                 VkImageTiling tiling,
                                 VkImageUsageFlags usage,
                                 VkMemoryPropertyFlags properties,
                                 VkImage& image,
                                 VkDeviceMemory& imageMemory) const
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    Check(vkCreateImage(device_, &imageInfo, nullptr, &image), "Failed to create image.");

    VkMemoryRequirements memoryRequirements{};
    vkGetImageMemoryRequirements(device_, image, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = FindMemoryType(memoryRequirements.memoryTypeBits, properties);

    Check(vkAllocateMemory(device_, &allocateInfo, nullptr, &imageMemory), "Failed to allocate image memory.");
    Check(vkBindImageMemory(device_, image, imageMemory, 0), "Failed to bind image memory.");
}

VkImageView VulkanRenderer::CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) const
{
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = image;
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = format;
    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.subresourceRange.aspectMask = aspectFlags;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    VkImageView imageView = VK_NULL_HANDLE;
    Check(vkCreateImageView(device_, &createInfo, nullptr, &imageView), "Failed to create image view.");
    return imageView;
}

std::uint32_t VulkanRenderer::FindMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);

    for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        const bool typeMatches = (typeFilter & (1u << i)) != 0;
        const bool propertiesMatch = (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;
        if (typeMatches && propertiesMatch)
        {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type.");
}

VulkanRenderer::QueueFamilyIndices VulkanRenderer::FindQueueFamilies(VkPhysicalDevice device) const
{
    QueueFamilyIndices indices{};

    std::uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (std::uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
        {
            indices.graphicsFamily = i;
            indices.hasGraphicsFamily = true;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
        if (presentSupport == VK_TRUE)
        {
            indices.presentFamily = i;
            indices.hasPresentFamily = true;
        }

        if (indices.IsComplete())
        {
            break;
        }
    }

    return indices;
}

VulkanRenderer::SwapchainSupportDetails VulkanRenderer::QuerySwapchainSupport(VkPhysicalDevice device) const
{
    SwapchainSupportDetails details{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &details.capabilities);

    std::uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);
    if (formatCount > 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, details.formats.data());
    }

    std::uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, nullptr);
    if (presentModeCount > 0)
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, details.presentModes.data());
    }

    return details;
}

bool VulkanRenderer::IsDeviceSuitable(VkPhysicalDevice device) const
{
    const QueueFamilyIndices indices = FindQueueFamilies(device);
    const bool extensionsSupported = CheckDeviceExtensionSupport(device);
    bool swapchainAdequate = false;
    bool dynamicRenderingSupported = false;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    if (properties.apiVersion >= VK_API_VERSION_1_3)
    {
        VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{};
        dynamicRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;

        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &dynamicRenderingFeatures;
        vkGetPhysicalDeviceFeatures2(device, &features);

        dynamicRenderingSupported = dynamicRenderingFeatures.dynamicRendering == VK_TRUE;
    }

    if (extensionsSupported)
    {
        const SwapchainSupportDetails swapchainSupport = QuerySwapchainSupport(device);
        swapchainAdequate = !swapchainSupport.formats.empty() && !swapchainSupport.presentModes.empty();
    }

    return indices.IsComplete() && extensionsSupported && swapchainAdequate && dynamicRenderingSupported;
}

bool VulkanRenderer::CheckDeviceExtensionSupport(VkPhysicalDevice device) const
{
    std::uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(DeviceExtensions.begin(), DeviceExtensions.end());
    for (const auto& extension : availableExtensions)
    {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

VkSurfaceFormatKHR VulkanRenderer::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const
{
    const auto preferred = std::find_if(formats.begin(), formats.end(), [](const VkSurfaceFormatKHR& format) {
        return format.format == VK_FORMAT_B8G8R8A8_UNORM &&
               format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });

    if (preferred != formats.end())
    {
        return *preferred;
    }

    const auto srgbFallback = std::find_if(formats.begin(), formats.end(), [](const VkSurfaceFormatKHR& format) {
        return format.format == VK_FORMAT_B8G8R8A8_SRGB &&
               format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });

    return srgbFallback != formats.end() ? *srgbFallback : formats.front();
}

VkPresentModeKHR VulkanRenderer::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes) const
{
    const auto preferred = std::find(presentModes.begin(), presentModes.end(), VK_PRESENT_MODE_MAILBOX_KHR);
    return preferred != presentModes.end() ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRenderer::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
{
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
    {
        return capabilities.currentExtent;
    }

    const auto framebufferSize = window_->GetFramebufferSize();
    VkExtent2D actualExtent{framebufferSize.width, framebufferSize.height};
    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return actualExtent;
}
}
