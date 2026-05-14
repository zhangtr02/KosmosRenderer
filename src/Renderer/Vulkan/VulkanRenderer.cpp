#include "Renderer/Vulkan/VulkanRenderer.h"

#include "Platform/Window.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

void Check(VkResult result, const char* message)
{
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error(message);
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
    CreateGraphicsPipeline();
    CreateCommandPool();
    CreateCommandBuffers();
    CreateSyncObjects();

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

    Check(vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, std::numeric_limits<std::uint64_t>::max()),
          "Failed to wait for frame fence.");

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

    const auto clearColor = scene.GetClearColor();
    VkClearColorValue vkClearColor{};
    vkClearColor.float32[0] = clearColor.r;
    vkClearColor.float32[1] = clearColor.g;
    vkClearColor.float32[2] = clearColor.b;
    vkClearColor.float32[3] = clearColor.a;

    RecordTrianglePass(commandBuffers_[currentFrame_], currentImageIndex_, vkClearColor);
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
        VkClearColorValue fallbackClear{};
        fallbackClear.float32[0] = 0.02f;
        fallbackClear.float32[1] = 0.03f;
        fallbackClear.float32[2] = 0.05f;
        fallbackClear.float32[3] = 1.0f;
        RecordTrianglePass(commandBuffers_[currentFrame_], currentImageIndex_, fallbackClear);
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
    CleanupSwapchain();

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
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchainImages_[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchainImageFormat_;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        Check(vkCreateImageView(device_, &createInfo, nullptr, &swapchainImageViews_[i]),
              "Failed to create swapchain image view.");
    }
}

void VulkanRenderer::CreateGraphicsPipeline()
{
    const std::vector<std::uint32_t> vertexShaderCode = ReadSpirvFile(ShaderPath("triangle.vert.spv"));
    const std::vector<std::uint32_t> fragmentShaderCode = ReadSpirvFile(ShaderPath("triangle.frag.spv"));

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

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    Check(vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_), "Failed to create pipeline layout.");

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

void VulkanRenderer::CleanupSwapchain()
{
    CleanupSwapchainSyncObjects();
    CleanupGraphicsPipeline();

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
    CreateGraphicsPipeline();
    CreateSwapchainSyncObjects();
}

void VulkanRenderer::RecordTrianglePass(VkCommandBuffer commandBuffer, std::uint32_t imageIndex, VkClearColorValue clearColor)
{
    VkClearValue clearValue{};
    clearValue.color = clearColor;

    TransitionSwapchainImageLayout(commandBuffer, imageIndex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

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
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);

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

    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRendering(commandBuffer);
    TransitionSwapchainImageLayout(commandBuffer, imageIndex, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
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

void VulkanRenderer::CleanupGraphicsPipeline()
{
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
        return format.format == VK_FORMAT_B8G8R8A8_SRGB &&
               format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });

    return preferred != formats.end() ? *preferred : formats.front();
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
