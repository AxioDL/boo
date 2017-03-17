#include "boo/graphicsdev/Vulkan.hpp"
#include "boo/IGraphicsContext.hpp"
#include <vector>
#include <array>
#include <cmath>
#include <glslang/Public/ShaderLang.h>
#include <StandAlone/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <SPIRV/disassemble.h>
#include "boo/graphicsdev/GLSLMacros.hpp"
#include "Common.hpp"
#include "xxhash.h"

#include "logvisor/logvisor.hpp"

#undef min
#undef max
#undef None

namespace boo
{
static logvisor::Module Log("boo::Vulkan");
VulkanContext g_VulkanContext;
class VulkanDataFactoryImpl;

struct VulkanShareableShader : IShareableShader<VulkanDataFactoryImpl, VulkanShareableShader>
{
    VkDevice m_dev;
    VkShaderModule m_shader;
    VulkanShareableShader(VulkanDataFactoryImpl& fac, uint64_t srcKey, uint64_t binKey,
                          VkDevice dev, VkShaderModule s)
    : IShareableShader(fac, srcKey, binKey), m_dev(dev), m_shader(s) {}
    ~VulkanShareableShader() { vk::DestroyShaderModule(m_dev, m_shader, nullptr); }
};

class VulkanDataFactoryImpl : public VulkanDataFactory
{
    friend struct VulkanCommandQueue;
    friend class VulkanDataFactory::Context;
    IGraphicsContext* m_parent;
    VulkanContext* m_ctx;
    uint32_t m_drawSamples;
    static ThreadLocalPtr<struct VulkanData> m_deferredData;
    std::unordered_set<struct VulkanData*> m_committedData;
    std::unordered_set<struct VulkanPool*> m_committedPools;
    std::mutex m_committedMutex;
    std::unordered_map<uint64_t, std::unique_ptr<VulkanShareableShader>> m_sharedShaders;
    std::vector<int> m_texUnis;
    void destroyData(IGraphicsData*);
    void destroyPool(IGraphicsBufferPool*);
    void destroyAllData();
    IGraphicsBufferD* newPoolBuffer(IGraphicsBufferPool *pool, BufferUse use,
                                    size_t stride, size_t count);
    void deletePoolBuffer(IGraphicsBufferPool* p, IGraphicsBufferD* buf);
public:
    std::unordered_map<uint64_t, uint64_t> m_sourceToBinary;
    VulkanDataFactoryImpl(IGraphicsContext* parent, VulkanContext* ctx, uint32_t drawSamples);
    ~VulkanDataFactoryImpl() {destroyAllData();}

    Platform platform() const {return Platform::Vulkan;}
    const SystemChar* platformName() const {return _S("Vulkan");}

    GraphicsDataToken commitTransaction(const FactoryCommitFunc&);
    GraphicsBufferPoolToken newBufferPool();

    void _unregisterShareableShader(uint64_t srcKey, uint64_t binKey)
    {
        if (srcKey)
            m_sourceToBinary.erase(srcKey);
        m_sharedShaders.erase(binKey);
    }
};

static inline void ThrowIfFailed(VkResult res)
{
    if (res != VK_SUCCESS)
        Log.report(logvisor::Fatal, "%d\n", res);
}

static inline void ThrowIfFalse(bool res)
{
    if (!res)
        Log.report(logvisor::Fatal, "operation failed\n", res);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
dbgFunc(VkDebugReportFlagsEXT msgFlags, VkDebugReportObjectTypeEXT objType,
        uint64_t srcObject, size_t location, int32_t msgCode,
        const char *pLayerPrefix, const char *pMsg, void *pUserData)
{
    if (msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
        Log.report(logvisor::Fatal, "[%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
    } else if (msgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
        Log.report(logvisor::Warning, "[%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
    } else if (msgFlags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
        Log.report(logvisor::Warning, "[%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
    } else if (msgFlags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
        Log.report(logvisor::Info, "[%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
    } else if (msgFlags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
        Log.report(logvisor::Info, "[%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
    }

    /*
     * false indicates that layer should not bail-out of an
     * API call that had validation failures. This may mean that the
     * app dies inside the driver due to invalid parameter(s).
     * That's what would happen without validation layers, so we'll
     * keep that behavior here.
     */
    return false;
}

static bool MemoryTypeFromProperties(VulkanContext* ctx, uint32_t typeBits,
                                     VkFlags requirementsMask,
                                     uint32_t *typeIndex)
{
    /* Search memtypes to find first index with those properties */
    for (uint32_t i = 0; i < 32; i++)
    {
        if ((typeBits & 1) == 1)
        {
            /* Type is available, does it match user properties? */
            if ((ctx->m_memoryProperties.memoryTypes[i].propertyFlags &
                 requirementsMask) == requirementsMask) {
                *typeIndex = i;
                return true;
            }
        }
        typeBits >>= 1;
    }
    /* No memory types matched, return failure */
    return false;
}

static void SetImageLayout(VkCommandBuffer cmd, VkImage image,
                           VkImageAspectFlags aspectMask,
                           VkImageLayout old_image_layout,
                           VkImageLayout new_image_layout,
                           uint32_t mipCount, uint32_t layerCount)
{
    VkImageMemoryBarrier imageMemoryBarrier = {};
    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageMemoryBarrier.pNext = NULL;
    imageMemoryBarrier.srcAccessMask = 0;
    imageMemoryBarrier.dstAccessMask = 0;
    imageMemoryBarrier.oldLayout = old_image_layout;
    imageMemoryBarrier.newLayout = new_image_layout;
    imageMemoryBarrier.image = image;
    imageMemoryBarrier.subresourceRange.aspectMask = aspectMask;
    imageMemoryBarrier.subresourceRange.baseMipLevel = 0;
    imageMemoryBarrier.subresourceRange.levelCount = mipCount;
    imageMemoryBarrier.subresourceRange.layerCount = layerCount;
    imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    VkPipelineStageFlags src_stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dest_stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    switch (old_image_layout)
    {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        imageMemoryBarrier.srcAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        src_stages = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
        break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        src_stages = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
        break;
    case VK_IMAGE_LAYOUT_PREINITIALIZED:
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        src_stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stages = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
        break;
    default: break;
    }

    switch (new_image_layout)
    {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dest_stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        dest_stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dest_stages = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
        break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        imageMemoryBarrier.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        dest_stages = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
        break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        imageMemoryBarrier.dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dest_stages = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
        break;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dest_stages = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
        break;
    default: break;
    }

    vk::CmdPipelineBarrier(cmd, src_stages, dest_stages, 0, 0, NULL, 0, NULL,
                           1, &imageMemoryBarrier);
}

static VkResult InitGlobalExtensionProperties(VulkanContext::LayerProperties& layerProps) {
    VkExtensionProperties *instance_extensions;
    uint32_t instance_extension_count;
    VkResult res;
    char *layer_name = nullptr;

    layer_name = layerProps.properties.layerName;

    do {
        res = vk::EnumerateInstanceExtensionProperties(
            layer_name, &instance_extension_count, nullptr);
        if (res)
            return res;

        if (instance_extension_count == 0) {
            return VK_SUCCESS;
        }

        layerProps.extensions.resize(instance_extension_count);
        instance_extensions = layerProps.extensions.data();
        res = vk::EnumerateInstanceExtensionProperties(
            layer_name, &instance_extension_count, instance_extensions);
    } while (res == VK_INCOMPLETE);

    return res;
}

/*
 * Return 1 (true) if all layer names specified in check_names
 * can be found in given layer properties.
 */
static void demo_check_layers(const std::vector<VulkanContext::LayerProperties>& layerProps,
                              const std::vector<const char*> &layerNames) {
    uint32_t check_count = layerNames.size();
    uint32_t layer_count = layerProps.size();
    for (uint32_t i = 0; i < check_count; i++) {
        VkBool32 found = 0;
        for (uint32_t j = 0; j < layer_count; j++) {
            if (!strcmp(layerNames[i], layerProps[j].properties.layerName)) {
                found = 1;
            }
        }
        if (!found) {
            Log.report(logvisor::Fatal, "Cannot find layer: %s", layerNames[i]);
        }
    }
}

void VulkanContext::initVulkan(const char* appName)
{
    if (!glslang::InitializeProcess())
        Log.report(logvisor::Fatal, "unable to initialize glslang");

    uint32_t instanceLayerCount;
    VkLayerProperties* vkProps = nullptr;
    VkResult res;

    /*
     * It's possible, though very rare, that the number of
     * instance layers could change. For example, installing something
     * could include new layers that the loader would pick up
     * between the initial query for the count and the
     * request for VkLayerProperties. The loader indicates that
     * by returning a VK_INCOMPLETE status and will update the
     * the count parameter.
     * The count parameter will be updated with the number of
     * entries loaded into the data pointer - in case the number
     * of layers went down or is smaller than the size given.
     */
#ifdef _WIN32
    char* vkSdkPath = getenv("VK_SDK_PATH");
    if (vkSdkPath)
    {
        std::string str = "VK_LAYER_PATH=";
        str += vkSdkPath;
        str += "\\Bin";
        _putenv(str.c_str());
    }
#else
    setenv("VK_LAYER_PATH", "/usr/share/vulkan/explicit_layer.d", 1);
#endif
    do {
        ThrowIfFailed(vk::EnumerateInstanceLayerProperties(&instanceLayerCount, nullptr));

        if (instanceLayerCount == 0)
            break;

        vkProps = (VkLayerProperties *)realloc(vkProps, instanceLayerCount * sizeof(VkLayerProperties));

        res = vk::EnumerateInstanceLayerProperties(&instanceLayerCount, vkProps);
    } while (res == VK_INCOMPLETE);

    /*
     * Now gather the extension list for each instance layer.
     */
    for (uint32_t i=0 ; i<instanceLayerCount ; ++i)
    {
        LayerProperties layerProps;
        layerProps.properties = vkProps[i];
        ThrowIfFailed(InitGlobalExtensionProperties(layerProps));
        m_instanceLayerProperties.push_back(layerProps);
    }
    free(vkProps);

    /* need platform surface extensions */
    m_instanceExtensionNames.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#ifdef _WIN32
    m_instanceExtensionNames.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#else
    m_instanceExtensionNames.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#endif

    /* need swapchain device extension */
    m_deviceExtensionNames.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

#ifndef NDEBUG
    m_layerNames.push_back("VK_LAYER_LUNARG_core_validation");
    m_layerNames.push_back("VK_LAYER_LUNARG_object_tracker");
    m_layerNames.push_back("VK_LAYER_LUNARG_parameter_validation");
    m_layerNames.push_back("VK_LAYER_LUNARG_swapchain");
    m_layerNames.push_back("VK_LAYER_GOOGLE_threading");
#endif

    demo_check_layers(m_instanceLayerProperties, m_layerNames);

#ifndef NDEBUG
    /* Enable debug callback extension */
    m_instanceExtensionNames.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
#endif

    /* create the instance */
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pNext = nullptr;
    appInfo.pApplicationName = appName;
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = "Boo";
    appInfo.engineVersion = 1;
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instInfo = {};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pNext = nullptr;
    instInfo.flags = 0;
    instInfo.pApplicationInfo = &appInfo;
    instInfo.enabledLayerCount = m_layerNames.size();
    instInfo.ppEnabledLayerNames = m_layerNames.size()
                                        ? m_layerNames.data()
                                        : nullptr;
    instInfo.enabledExtensionCount = m_instanceExtensionNames.size();
    instInfo.ppEnabledExtensionNames = m_instanceExtensionNames.data();

    VkResult instRes = vk::CreateInstance(&instInfo, nullptr, &m_instance);
    if (instRes != VK_SUCCESS)
    {
#ifdef _WIN32
        MessageBoxW(nullptr, L"Error creating Vulkan instance\n\n"
                             L"The Vulkan runtime is installed, but there are no supported "
                             L"hardware vendor interfaces present",
                             L"Vulkan Error", MB_OK | MB_ICONERROR);
#else
        Log.report(logvisor::Fatal, "The Vulkan runtime is installed, but there are no supported "
                                    "hardware vendor interfaces present");
#endif
        exit(1);
    }

#ifndef NDEBUG
    VkDebugReportCallbackEXT debugReportCallback;

    PFN_vkCreateDebugReportCallbackEXT createDebugReportCallback =
        (PFN_vkCreateDebugReportCallbackEXT)vk::GetInstanceProcAddr(m_instance, "vkCreateDebugReportCallbackEXT");
    if (!createDebugReportCallback)
        Log.report(logvisor::Fatal, "GetInstanceProcAddr: Unable to find vkCreateDebugReportCallbackEXT function.");

    VkDebugReportCallbackCreateInfoEXT debugCreateInfo = {};
    debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
    debugCreateInfo.pNext = nullptr;
    debugCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
    debugCreateInfo.pfnCallback = dbgFunc;
    debugCreateInfo.pUserData = nullptr;
    ThrowIfFailed(createDebugReportCallback(m_instance, &debugCreateInfo, nullptr, &debugReportCallback));
#endif
}

bool VulkanContext::enumerateDevices()
{
    uint32_t gpuCount = 1;
    ThrowIfFailed(vk::EnumeratePhysicalDevices(m_instance, &gpuCount, nullptr));
    if (!gpuCount)
        return false;
    m_gpus.resize(gpuCount);

    ThrowIfFailed(vk::EnumeratePhysicalDevices(m_instance, &gpuCount, m_gpus.data()));
    if (!gpuCount)
        return false;

    vk::GetPhysicalDeviceQueueFamilyProperties(m_gpus[0], &m_queueCount, nullptr);
    if (!m_queueCount)
        return false;

    m_queueProps.resize(m_queueCount);
    vk::GetPhysicalDeviceQueueFamilyProperties(m_gpus[0], &m_queueCount, m_queueProps.data());
    if (!m_queueCount)
        return false;

    /* This is as good a place as any to do this */
    vk::GetPhysicalDeviceMemoryProperties(m_gpus[0], &m_memoryProperties);
    vk::GetPhysicalDeviceProperties(m_gpus[0], &m_gpuProps);

    return true;
}

void VulkanContext::initDevice()
{
    if (m_graphicsQueueFamilyIndex == UINT32_MAX)
        Log.report(logvisor::Fatal,
                   "VulkanContext::m_graphicsQueueFamilyIndex hasn't been initialized");

    /* create the device and queues */
    VkDeviceQueueCreateInfo queueInfo = {};
    float queuePriorities[1] = {0.0};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.pNext = nullptr;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = queuePriorities;
    queueInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;

    VkDeviceCreateInfo deviceInfo = {};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = nullptr;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledLayerCount = m_layerNames.size();
    deviceInfo.ppEnabledLayerNames =
        deviceInfo.enabledLayerCount ? m_layerNames.data() : nullptr;
    deviceInfo.enabledExtensionCount = m_deviceExtensionNames.size();
    deviceInfo.ppEnabledExtensionNames =
        deviceInfo.enabledExtensionCount ? m_deviceExtensionNames.data() : nullptr;
    deviceInfo.pEnabledFeatures = nullptr;

    ThrowIfFailed(vk::CreateDevice(m_gpus[0], &deviceInfo, nullptr, &m_dev));
}

void VulkanContext::initSwapChain(VulkanContext::Window& windowCtx, VkSurfaceKHR surface, VkFormat format, VkColorSpaceKHR colorspace)
{
    m_displayFormat = format;
    VkSurfaceCapabilitiesKHR surfCapabilities;
    ThrowIfFailed(vk::GetPhysicalDeviceSurfaceCapabilitiesKHR(m_gpus[0], surface, &surfCapabilities));

    uint32_t presentModeCount;
    ThrowIfFailed(vk::GetPhysicalDeviceSurfacePresentModesKHR(m_gpus[0], surface, &presentModeCount, nullptr));
    std::unique_ptr<VkPresentModeKHR[]> presentModes(new VkPresentModeKHR[presentModeCount]);

    ThrowIfFailed(vk::GetPhysicalDeviceSurfacePresentModesKHR(m_gpus[0], surface, &presentModeCount, presentModes.get()));

    VkExtent2D swapChainExtent;
    // width and height are either both -1, or both not -1.
    if (surfCapabilities.currentExtent.width == (uint32_t)-1)
    {
        // If the surface size is undefined, the size is set to
        // the size of the images requested.
        swapChainExtent.width = 50;
        swapChainExtent.height = 50;
    }
    else
    {
        // If the surface size is defined, the swap chain size must match
        swapChainExtent = surfCapabilities.currentExtent;
    }

    // If mailbox mode is available, use it, as is the lowest-latency non-
    // tearing mode.  If not, try IMMEDIATE which will usually be available,
    // and is fastest (though it tears).  If not, fall back to FIFO which is
    // always available.
    VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (size_t i=0 ; i<presentModeCount ; ++i)
    {
        if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }
        if ((swapchainPresentMode != VK_PRESENT_MODE_MAILBOX_KHR) &&
            (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR))
        {
            swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
    }

    // Determine the number of VkImage's to use in the swap chain (we desire to
    // own only 1 image at a time, besides the images being displayed and
    // queued for display):
    uint32_t desiredNumberOfSwapChainImages = surfCapabilities.minImageCount + 1;
    if ((surfCapabilities.maxImageCount > 0) &&
        (desiredNumberOfSwapChainImages > surfCapabilities.maxImageCount))
    {
        // Application must settle for fewer images than desired:
        desiredNumberOfSwapChainImages = surfCapabilities.maxImageCount;
    }

    VkSurfaceTransformFlagBitsKHR preTransform;
    if (surfCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    else
        preTransform = surfCapabilities.currentTransform;

    VkSwapchainCreateInfoKHR swapChainInfo = {};
    swapChainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapChainInfo.pNext = nullptr;
    swapChainInfo.surface = surface;
    swapChainInfo.minImageCount = desiredNumberOfSwapChainImages;
    swapChainInfo.imageFormat = format;
    swapChainInfo.imageExtent.width = swapChainExtent.width;
    swapChainInfo.imageExtent.height = swapChainExtent.height;
    swapChainInfo.preTransform = preTransform;
    swapChainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapChainInfo.imageArrayLayers = 1;
    swapChainInfo.presentMode = swapchainPresentMode;
    swapChainInfo.oldSwapchain = nullptr;
    swapChainInfo.clipped = true;
    swapChainInfo.imageColorSpace = colorspace;
    swapChainInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapChainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapChainInfo.queueFamilyIndexCount = 0;
    swapChainInfo.pQueueFamilyIndices = nullptr;

    Window::SwapChain& sc = windowCtx.m_swapChains[windowCtx.m_activeSwapChain];
    ThrowIfFailed(vk::CreateSwapchainKHR(m_dev, &swapChainInfo, nullptr, &sc.m_swapChain));
    sc.m_format = format;

    uint32_t swapchainImageCount;
    ThrowIfFailed(vk::GetSwapchainImagesKHR(m_dev, sc.m_swapChain, &swapchainImageCount, nullptr));

    std::unique_ptr<VkImage[]> swapchainImages(new VkImage[swapchainImageCount]);
    ThrowIfFailed(vk::GetSwapchainImagesKHR(m_dev, sc.m_swapChain, &swapchainImageCount, swapchainImages.get()));

    // Going to need a command buffer to send the memory barriers in
    // set_image_layout but we couldn't have created one before we knew
    // what our graphics_queue_family_index is, but now that we have it,
    // create the command buffer

    VkCommandPoolCreateInfo cmdPoolInfo = {};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.pNext = nullptr;
    cmdPoolInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ThrowIfFailed(vk::CreateCommandPool(m_dev, &cmdPoolInfo, nullptr, &m_loadPool));

    VkCommandBufferAllocateInfo cmd = {};
    cmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd.pNext = nullptr;
    cmd.commandPool = m_loadPool;
    cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd.commandBufferCount = 1;
    ThrowIfFailed(vk::AllocateCommandBuffers(m_dev, &cmd, &m_loadCmdBuf));

    vk::GetDeviceQueue(m_dev, m_graphicsQueueFamilyIndex, 0, &m_queue);

    /* Begin load command buffer here */
    VkCommandBufferBeginInfo cmdBufBeginInfo = {};
    cmdBufBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufBeginInfo.flags = 0;
    ThrowIfFailed(vk::BeginCommandBuffer(m_loadCmdBuf, &cmdBufBeginInfo));

    /* Create shared linear sampler */
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.pNext = nullptr;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    ThrowIfFailed(vk::CreateSampler(m_dev, &samplerInfo, nullptr, &m_linearSampler));

    /* images */
    sc.m_bufs.resize(swapchainImageCount);
    for (uint32_t i=0 ; i<swapchainImageCount ; ++i)
    {
        Window::SwapChain::Buffer& buf = sc.m_bufs[i];
        buf.m_image = swapchainImages[i];
    }
}

void VulkanContext::resizeSwapChain(VulkanContext::Window& windowCtx, VkSurfaceKHR surface,
                                    VkFormat format, VkColorSpaceKHR colorspace,
                                    const SWindowRect& rect)
{
    std::unique_lock<std::mutex> lk(m_resizeLock);
    m_deferredResizes.emplace(windowCtx, surface, format, colorspace, rect);
}

bool VulkanContext::_resizeSwapChains()
{
    std::unique_lock<std::mutex> lk(m_resizeLock);
    if (m_deferredResizes.empty())
        return false;

    while (m_deferredResizes.size())
    {
        SwapChainResize& resize = m_deferredResizes.front();

        VkSurfaceCapabilitiesKHR surfCapabilities;
        ThrowIfFailed(vk::GetPhysicalDeviceSurfaceCapabilitiesKHR(m_gpus[0], resize.m_surface, &surfCapabilities));

        uint32_t presentModeCount;
        ThrowIfFailed(vk::GetPhysicalDeviceSurfacePresentModesKHR(m_gpus[0], resize.m_surface, &presentModeCount, nullptr));
        std::unique_ptr<VkPresentModeKHR[]> presentModes(new VkPresentModeKHR[presentModeCount]);

        ThrowIfFailed(vk::GetPhysicalDeviceSurfacePresentModesKHR(m_gpus[0], resize.m_surface, &presentModeCount, presentModes.get()));

        VkExtent2D swapChainExtent;
        // width and height are either both -1, or both not -1.
        if (surfCapabilities.currentExtent.width == (uint32_t)-1)
        {
            // If the surface size is undefined, the size is set to
            // the size of the images requested.
            swapChainExtent.width = 50;
            swapChainExtent.height = 50;
        }
        else
        {
            // If the surface size is defined, the swap chain size must match
            swapChainExtent = surfCapabilities.currentExtent;
        }

        // If mailbox mode is available, use it, as is the lowest-latency non-
        // tearing mode.  If not, try IMMEDIATE which will usually be available,
        // and is fastest (though it tears).  If not, fall back to FIFO which is
        // always available.
        VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;
        for (size_t i=0 ; i<presentModeCount ; ++i)
        {
            if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                break;
            }
            if ((swapchainPresentMode != VK_PRESENT_MODE_MAILBOX_KHR) &&
                (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR))
            {
                swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            }
        }

        // Determine the number of VkImage's to use in the swap chain (we desire to
        // own only 1 image at a time, besides the images being displayed and
        // queued for display):
        uint32_t desiredNumberOfSwapChainImages = surfCapabilities.minImageCount + 1;
        if ((surfCapabilities.maxImageCount > 0) &&
            (desiredNumberOfSwapChainImages > surfCapabilities.maxImageCount))
        {
            // Application must settle for fewer images than desired:
            desiredNumberOfSwapChainImages = surfCapabilities.maxImageCount;
        }

        VkSurfaceTransformFlagBitsKHR preTransform;
        if (surfCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
            preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        else
            preTransform = surfCapabilities.currentTransform;

        Window::SwapChain& oldSc = resize.m_windowCtx.m_swapChains[resize.m_windowCtx.m_activeSwapChain];

        VkSwapchainCreateInfoKHR swapChainInfo = {};
        swapChainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapChainInfo.pNext = nullptr;
        swapChainInfo.surface = resize.m_surface;
        swapChainInfo.minImageCount = desiredNumberOfSwapChainImages;
        swapChainInfo.imageFormat = resize.m_format;
        swapChainInfo.imageExtent.width = swapChainExtent.width;
        swapChainInfo.imageExtent.height = swapChainExtent.height;
        swapChainInfo.preTransform = preTransform;
        swapChainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapChainInfo.imageArrayLayers = 1;
        swapChainInfo.presentMode = swapchainPresentMode;
        swapChainInfo.oldSwapchain = oldSc.m_swapChain;
        swapChainInfo.clipped = true;
        swapChainInfo.imageColorSpace = resize.m_colorspace;
        swapChainInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        swapChainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapChainInfo.queueFamilyIndexCount = 0;
        swapChainInfo.pQueueFamilyIndices = nullptr;

        resize.m_windowCtx.m_activeSwapChain ^= 1;
        Window::SwapChain& sc = resize.m_windowCtx.m_swapChains[resize.m_windowCtx.m_activeSwapChain];
        sc.destroy(m_dev);
        ThrowIfFailed(vk::CreateSwapchainKHR(m_dev, &swapChainInfo, nullptr, &sc.m_swapChain));
        sc.m_format = resize.m_format;

        uint32_t swapchainImageCount;
        ThrowIfFailed(vk::GetSwapchainImagesKHR(m_dev, sc.m_swapChain, &swapchainImageCount, nullptr));

        std::unique_ptr<VkImage[]> swapchainImages(new VkImage[swapchainImageCount]);
        ThrowIfFailed(vk::GetSwapchainImagesKHR(m_dev, sc.m_swapChain, &swapchainImageCount, swapchainImages.get()));

        /* images */
        sc.m_bufs.resize(swapchainImageCount);
        for (uint32_t i=0 ; i<swapchainImageCount ; ++i)
        {
            Window::SwapChain::Buffer& buf = sc.m_bufs[i];
            buf.m_image = swapchainImages[i];
        }

        m_deferredResizes.pop();
    }

    return true;
}

struct VulkanData : IGraphicsDataPriv
{
    VulkanContext* m_ctx;
    VkDeviceMemory m_bufMem = VK_NULL_HANDLE;
    VkDeviceMemory m_texMem = VK_NULL_HANDLE;
    std::vector<std::unique_ptr<class VulkanShaderPipeline>> m_SPs;
    std::vector<std::unique_ptr<struct VulkanShaderDataBinding>> m_SBinds;
    std::vector<std::unique_ptr<class VulkanGraphicsBufferS>> m_SBufs;
    std::vector<std::unique_ptr<class VulkanGraphicsBufferD>> m_DBufs;
    std::vector<std::unique_ptr<class VulkanTextureS>> m_STexs;
    std::vector<std::unique_ptr<class VulkanTextureSA>> m_SATexs;
    std::vector<std::unique_ptr<class VulkanTextureD>> m_DTexs;
    std::vector<std::unique_ptr<class VulkanTextureR>> m_RTexs;
    std::vector<std::unique_ptr<struct VulkanVertexFormat>> m_VFmts;
    bool m_dead = false;
    VulkanData(VulkanContext* ctx) : m_ctx(ctx) {}
    ~VulkanData()
    {
        if (m_bufMem)
            vk::FreeMemory(m_ctx->m_dev, m_bufMem, nullptr);
        if (m_texMem)
            vk::FreeMemory(m_ctx->m_dev, m_texMem, nullptr);
    }
};

struct VulkanPoolItem : IGraphicsDataPriv
{
    VulkanContext* m_ctx;
    VkDeviceMemory m_bufMem = VK_NULL_HANDLE;
    std::unique_ptr<class VulkanGraphicsBufferD> m_buf;
    bool m_dead = false;
    VulkanPoolItem(VulkanContext* ctx) : m_ctx(ctx) {}
    ~VulkanPoolItem()
    {
        if (m_bufMem)
            vk::FreeMemory(m_ctx->m_dev, m_bufMem, nullptr);
    }
};

struct VulkanPool : IGraphicsBufferPool
{
    std::unordered_set<VulkanPoolItem*> m_items;
    bool m_dead = false;
    ~VulkanPool()
    {
        for (auto& item : m_items)
            item->decrement();
    }

    void clearDeadBuffers()
    {
        for (auto it = m_items.begin() ; it != m_items.end() ;)
        {
            if ((*it)->m_dead)
            {
                (*it)->decrement();
                it = m_items.erase(it);
                continue;
            }
            ++it;
        }
    }
};

static const VkBufferUsageFlagBits USE_TABLE[] =
{
    VkBufferUsageFlagBits(0),
    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
    VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
};

class VulkanGraphicsBufferS : public IGraphicsBufferS
{
    friend class VulkanDataFactory;
    friend struct VulkanCommandQueue;
    VulkanContext* m_ctx;
    size_t m_sz;
    std::unique_ptr<uint8_t[]> m_stagingBuf;
    VulkanGraphicsBufferS(IGraphicsData* parent, BufferUse use, VulkanContext* ctx,
                          const void* data, size_t stride, size_t count)
    : IGraphicsBufferS(parent), m_ctx(ctx), m_stride(stride), m_count(count), m_sz(stride * count),
      m_stagingBuf(new uint8_t[m_sz]), m_uniform(use == BufferUse::Uniform)
    {
        memmove(m_stagingBuf.get(), data, m_sz);

        VkBufferCreateInfo bufInfo = {};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.pNext = nullptr;
        bufInfo.usage = USE_TABLE[int(use)];
        bufInfo.size = m_sz;
        bufInfo.queueFamilyIndexCount = 0;
        bufInfo.pQueueFamilyIndices = nullptr;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufInfo.flags = 0;
        ThrowIfFailed(vk::CreateBuffer(ctx->m_dev, &bufInfo, nullptr, &m_bufferInfo.buffer));
        m_bufferInfo.offset = 0;
        m_bufferInfo.range = m_sz;
    }
public:
    size_t size() const {return m_sz;}
    size_t m_stride;
    size_t m_count;
    VkDescriptorBufferInfo m_bufferInfo;
    VkDeviceSize m_memOffset;
    bool m_uniform = false;
    ~VulkanGraphicsBufferS()
    {
        vk::DestroyBuffer(m_ctx->m_dev, m_bufferInfo.buffer, nullptr);
    }

    VkDeviceSize sizeForGPU(VulkanContext* ctx, uint32_t& memTypeBits, VkDeviceSize offset)
    {
        if (m_uniform)
        {
            size_t minOffset = std::max(VkDeviceSize(256),
                ctx->m_gpuProps.limits.minUniformBufferOffsetAlignment);
            offset = (offset + minOffset - 1) & ~(minOffset - 1);
        }

        VkMemoryRequirements memReqs;
        vk::GetBufferMemoryRequirements(ctx->m_dev, m_bufferInfo.buffer, &memReqs);
        memTypeBits &= memReqs.memoryTypeBits;
        m_memOffset = offset;

        offset += m_sz;
        offset = (offset + memReqs.alignment - 1) & ~(memReqs.alignment - 1);

        return offset;
    }

    void placeForGPU(VulkanContext* ctx, VkDeviceMemory mem, uint8_t* buf)
    {
        memmove(buf + m_memOffset, m_stagingBuf.get(), m_sz);
        m_stagingBuf.reset();
        ThrowIfFailed(vk::BindBufferMemory(ctx->m_dev, m_bufferInfo.buffer, mem, m_memOffset));
    }
};

class VulkanGraphicsBufferD : public IGraphicsBufferD
{
    friend class VulkanDataFactory;
    friend class VulkanDataFactoryImpl;
    friend struct VulkanCommandQueue;
    struct VulkanCommandQueue* m_q;
    size_t m_cpuSz;
    std::unique_ptr<uint8_t[]> m_cpuBuf;
    int m_validSlots = 0;
    VulkanGraphicsBufferD(IGraphicsData* parent, VulkanCommandQueue* q, BufferUse use,
                          VulkanContext* ctx, size_t stride, size_t count)
    : IGraphicsBufferD(parent), m_q(q), m_stride(stride), m_count(count),
      m_cpuSz(stride * count), m_cpuBuf(new uint8_t[m_cpuSz]),
      m_uniform(use == BufferUse::Uniform)
    {
        VkBufferCreateInfo bufInfo = {};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.pNext = nullptr;
        bufInfo.usage = USE_TABLE[int(use)];
        bufInfo.size = m_cpuSz;
        bufInfo.queueFamilyIndexCount = 0;
        bufInfo.pQueueFamilyIndices = nullptr;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufInfo.flags = 0;
        ThrowIfFailed(vk::CreateBuffer(ctx->m_dev, &bufInfo, nullptr, &m_bufferInfo[0].buffer));
        ThrowIfFailed(vk::CreateBuffer(ctx->m_dev, &bufInfo, nullptr, &m_bufferInfo[1].buffer));
        m_bufferInfo[0].offset = 0;
        m_bufferInfo[0].range = m_cpuSz;
        m_bufferInfo[1].offset = 0;
        m_bufferInfo[1].range = m_cpuSz;
    }
    void update(int b);

public:
    size_t m_stride;
    size_t m_count;
    VkDeviceMemory m_mem;
    VkDeviceSize m_memOffset[2];
    VkDescriptorBufferInfo m_bufferInfo[2];
    bool m_uniform = false;
    ~VulkanGraphicsBufferD();
    void load(const void* data, size_t sz);
    void* map(size_t sz);
    void unmap();

    VkDeviceSize sizeForGPU(VulkanContext* ctx, uint32_t& memTypeBits, VkDeviceSize offset)
    {
        for (int i=0 ; i<2 ; ++i)
        {
            if (m_uniform)
            {
                size_t minOffset = std::max(VkDeviceSize(256),
                    ctx->m_gpuProps.limits.minUniformBufferOffsetAlignment);
                offset = (offset + minOffset - 1) & ~(minOffset - 1);
            }

            VkMemoryRequirements memReqs;
            vk::GetBufferMemoryRequirements(ctx->m_dev, m_bufferInfo[i].buffer, &memReqs);
            memTypeBits &= memReqs.memoryTypeBits;
            m_memOffset[i] = offset;

            offset += memReqs.size;
            offset = (offset + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
        }

        return offset;
    }

    void placeForGPU(VulkanContext* ctx, VkDeviceMemory mem)
    {
        m_mem = mem;
        ThrowIfFailed(vk::BindBufferMemory(ctx->m_dev, m_bufferInfo[0].buffer, mem, m_memOffset[0]));
        ThrowIfFailed(vk::BindBufferMemory(ctx->m_dev, m_bufferInfo[1].buffer, mem, m_memOffset[1]));
    }
};

class VulkanTextureS : public ITextureS
{
    friend class VulkanDataFactory;
    VulkanContext* m_ctx;
    TextureFormat m_fmt;
    size_t m_sz;
    size_t m_width, m_height, m_mips;
    VkFormat m_vkFmt;
    int m_pixelPitchNum = 1;
    int m_pixelPitchDenom = 1;

    VulkanTextureS(IGraphicsData* parent, VulkanContext* ctx,
                   size_t width, size_t height, size_t mips,
                   TextureFormat fmt, const void* data, size_t sz)
    : ITextureS(parent), m_ctx(ctx), m_fmt(fmt), m_sz(sz), m_width(width), m_height(height), m_mips(mips)
    {
        VkFormat pfmt;
        switch (fmt)
        {
        case TextureFormat::RGBA8:
            pfmt = VK_FORMAT_R8G8B8A8_UNORM;
            m_pixelPitchNum = 4;
            break;
        case TextureFormat::I8:
            pfmt = VK_FORMAT_R8_UNORM;
            break;
        case TextureFormat::DXT1:
            pfmt = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
            m_pixelPitchNum = 1;
            m_pixelPitchDenom = 2;
            break;
        default:
            Log.report(logvisor::Fatal, "unsupported tex format");
        }
        m_vkFmt = pfmt;

        /* create cpu image buffer */
        VkBufferCreateInfo bufCreateInfo = {};
        bufCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufCreateInfo.size = sz;
        bufCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ThrowIfFailed(vk::CreateBuffer(ctx->m_dev, &bufCreateInfo, nullptr, &m_cpuBuf));

        VkMemoryRequirements memReqs;
        vk::GetBufferMemoryRequirements(ctx->m_dev, m_cpuBuf, &memReqs);

        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAlloc.pNext = nullptr;
        memAlloc.memoryTypeIndex = 0;
        memAlloc.allocationSize = memReqs.size;
        ThrowIfFalse(MemoryTypeFromProperties(ctx, memReqs.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                              &memAlloc.memoryTypeIndex));

        /* allocate memory */
        ThrowIfFailed(vk::AllocateMemory(ctx->m_dev, &memAlloc, nullptr, &m_cpuMem));

        /* bind memory */
        ThrowIfFailed(vk::BindBufferMemory(ctx->m_dev, m_cpuBuf, m_cpuMem, 0));

        /* map memory and copy data */
        uint8_t* mappedData;
        ThrowIfFailed(vk::MapMemory(ctx->m_dev, m_cpuMem, 0, memReqs.size, 0, reinterpret_cast<void**>(&mappedData)));
        memmove(mappedData, data, sz);
        vk::UnmapMemory(ctx->m_dev, m_cpuMem);

        /* create gpu image */
        VkImageCreateInfo texCreateInfo = {};
        texCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        texCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        texCreateInfo.format = pfmt;
        texCreateInfo.mipLevels = mips;
        texCreateInfo.arrayLayers = 1;
        texCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        texCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        texCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        texCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        texCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        texCreateInfo.extent = { uint32_t(m_width), uint32_t(m_height), 1 };
        texCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ThrowIfFailed(vk::CreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_gpuTex));

        m_descInfo.sampler = ctx->m_linearSampler;
        m_descInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
public:
    VkBuffer m_cpuBuf;
    VkDeviceMemory m_cpuMem;
    VkImage m_gpuTex;
    VkImageView m_gpuView = VK_NULL_HANDLE;
    VkDescriptorImageInfo m_descInfo;
    VkDeviceSize m_gpuOffset;
    ~VulkanTextureS()
    {
        vk::DestroyImageView(m_ctx->m_dev, m_gpuView, nullptr);
        vk::DestroyImage(m_ctx->m_dev, m_gpuTex, nullptr);
        if (m_cpuBuf)
            vk::DestroyBuffer(m_ctx->m_dev, m_cpuBuf, nullptr);
        if (m_cpuMem)
            vk::FreeMemory(m_ctx->m_dev, m_cpuMem, nullptr);
    }

    void deleteUploadObjects()
    {
        vk::DestroyBuffer(m_ctx->m_dev, m_cpuBuf, nullptr);
        m_cpuBuf = VK_NULL_HANDLE;
        vk::FreeMemory(m_ctx->m_dev, m_cpuMem, nullptr);
        m_cpuMem = VK_NULL_HANDLE;
    }

    VkDeviceSize sizeForGPU(VulkanContext* ctx, uint32_t& memTypeBits, VkDeviceSize offset)
    {
        VkMemoryRequirements memReqs;
        vk::GetImageMemoryRequirements(ctx->m_dev, m_gpuTex, &memReqs);
        memTypeBits &= memReqs.memoryTypeBits;

        m_gpuOffset = offset;
        offset += memReqs.size;
        offset = (offset + memReqs.alignment - 1) & ~(memReqs.alignment - 1);

        return offset;
    }

    void placeForGPU(VulkanContext* ctx, VkDeviceMemory mem)
    {
        /* bind memory */
        ThrowIfFailed(vk::BindImageMemory(ctx->m_dev, m_gpuTex, mem, m_gpuOffset));

        /* create image view */
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.pNext = nullptr;
        viewInfo.image = m_gpuTex;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_vkFmt;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = m_mips;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        ThrowIfFailed(vk::CreateImageView(ctx->m_dev, &viewInfo, nullptr, &m_gpuView));
        m_descInfo.imageView = m_gpuView;

        /* Since we're going to blit to the texture image, set its layout to
         * DESTINATION_OPTIMAL */
        SetImageLayout(ctx->m_loadCmdBuf, m_gpuTex, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_mips, 1);

        VkBufferImageCopy copyRegions[16] = {};
        size_t width = m_width;
        size_t height = m_height;
        size_t regionCount = std::min(size_t(16), m_mips);
        size_t offset = 0;
        for (int i=0 ; i<regionCount ; ++i)
        {
            size_t srcRowPitch = width * m_pixelPitchNum / m_pixelPitchDenom;

            copyRegions[i].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegions[i].imageSubresource.mipLevel = i;
            copyRegions[i].imageSubresource.baseArrayLayer = 0;
            copyRegions[i].imageSubresource.layerCount = 1;
            copyRegions[i].imageExtent.width = width;
            copyRegions[i].imageExtent.height = height;
            copyRegions[i].imageExtent.depth = 1;
            copyRegions[i].bufferOffset = offset;

            if (width > 1)
                width /= 2;
            if (height > 1)
                height /= 2;
            offset += srcRowPitch;
        }

        /* Put the copy command into the command buffer */
        vk::CmdCopyBufferToImage(ctx->m_loadCmdBuf,
                                 m_cpuBuf,
                                 m_gpuTex,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 regionCount,
                                 copyRegions);

        /* Set the layout for the texture image from DESTINATION_OPTIMAL to
         * SHADER_READ_ONLY */
        SetImageLayout(ctx->m_loadCmdBuf, m_gpuTex, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_mips, 1);
    }

    TextureFormat format() const {return m_fmt;}
};

class VulkanTextureSA : public ITextureSA
{
    friend class VulkanDataFactory;
    VulkanContext* m_ctx;
    TextureFormat m_fmt;
    size_t m_sz;
    size_t m_width, m_height, m_layers, m_mips;
    VkFormat m_vkFmt;
    int m_pixelPitchNum = 1;
    int m_pixelPitchDenom = 1;

    VulkanTextureSA(IGraphicsData* parent, VulkanContext* ctx,
                    size_t width, size_t height, size_t layers,
                    size_t mips, TextureFormat fmt, const void* data, size_t sz)
    : ITextureSA(parent), m_ctx(ctx), m_fmt(fmt), m_width(width),
      m_height(height), m_layers(layers), m_mips(mips), m_sz(sz)
    {
        VkFormat pfmt;
        switch (fmt)
        {
        case TextureFormat::RGBA8:
            pfmt = VK_FORMAT_R8G8B8A8_UNORM;
            m_pixelPitchNum = 4;
            break;
        case TextureFormat::I8:
            pfmt = VK_FORMAT_R8_UNORM;
            break;
        default:
            Log.report(logvisor::Fatal, "unsupported tex format");
        }
        m_vkFmt = pfmt;

        /* create cpu image buffer */
        VkBufferCreateInfo bufCreateInfo = {};
        bufCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufCreateInfo.size = sz;
        bufCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ThrowIfFailed(vk::CreateBuffer(ctx->m_dev, &bufCreateInfo, nullptr, &m_cpuBuf));

        VkMemoryRequirements memReqs;
        vk::GetBufferMemoryRequirements(ctx->m_dev, m_cpuBuf, &memReqs);

        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAlloc.pNext = nullptr;
        memAlloc.memoryTypeIndex = 0;
        memAlloc.allocationSize = memReqs.size;
        ThrowIfFalse(MemoryTypeFromProperties(ctx, memReqs.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                              &memAlloc.memoryTypeIndex));

        /* allocate memory */
        ThrowIfFailed(vk::AllocateMemory(ctx->m_dev, &memAlloc, nullptr, &m_cpuMem));

        /* bind memory */
        ThrowIfFailed(vk::BindBufferMemory(ctx->m_dev, m_cpuBuf, m_cpuMem, 0));

        /* map memory and copy data */
        uint8_t* mappedData;
        ThrowIfFailed(vk::MapMemory(ctx->m_dev, m_cpuMem, 0, memReqs.size, 0, reinterpret_cast<void**>(&mappedData)));
        memmove(mappedData, data, sz);
        vk::UnmapMemory(ctx->m_dev, m_cpuMem);

        /* create gpu image */
        VkImageCreateInfo texCreateInfo = {};
        texCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        texCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        texCreateInfo.format = pfmt;
        texCreateInfo.mipLevels = mips;
        texCreateInfo.arrayLayers = layers;
        texCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        texCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        texCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        texCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        texCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        texCreateInfo.extent = { uint32_t(m_width), uint32_t(m_height), 1 };
        texCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ThrowIfFailed(vk::CreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_gpuTex));

        m_descInfo.sampler = ctx->m_linearSampler;
        m_descInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
public:
    VkBuffer m_cpuBuf;
    VkDeviceMemory m_cpuMem;
    VkImage m_gpuTex;
    VkImageView m_gpuView = VK_NULL_HANDLE;
    VkDescriptorImageInfo m_descInfo;
    VkDeviceSize m_gpuOffset;
    ~VulkanTextureSA()
    {
        vk::DestroyImageView(m_ctx->m_dev, m_gpuView, nullptr);
        vk::DestroyImage(m_ctx->m_dev, m_gpuTex, nullptr);
        if (m_cpuBuf)
            vk::DestroyBuffer(m_ctx->m_dev, m_cpuBuf, nullptr);
        if (m_cpuMem)
            vk::FreeMemory(m_ctx->m_dev, m_cpuMem, nullptr);
    }

    void deleteUploadObjects()
    {
        vk::DestroyBuffer(m_ctx->m_dev, m_cpuBuf, nullptr);
        m_cpuBuf = VK_NULL_HANDLE;
        vk::FreeMemory(m_ctx->m_dev, m_cpuMem, nullptr);
        m_cpuMem = VK_NULL_HANDLE;
    }

    VkDeviceSize sizeForGPU(VulkanContext* ctx, uint32_t& memTypeBits, VkDeviceSize offset)
    {
        VkMemoryRequirements memReqs;
        vk::GetImageMemoryRequirements(ctx->m_dev, m_gpuTex, &memReqs);
        memTypeBits &= memReqs.memoryTypeBits;

        m_gpuOffset = offset;
        offset += memReqs.size;
        offset = (offset + memReqs.alignment - 1) & ~(memReqs.alignment - 1);

        return offset;
    }

    void placeForGPU(VulkanContext* ctx, VkDeviceMemory mem)
    {
        /* bind memory */
        ThrowIfFailed(vk::BindImageMemory(ctx->m_dev, m_gpuTex, mem, m_gpuOffset));

        /* create image view */
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.pNext = nullptr;
        viewInfo.image = m_gpuTex;
        viewInfo.viewType = (m_layers > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_vkFmt;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = m_mips;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = m_layers;

        ThrowIfFailed(vk::CreateImageView(ctx->m_dev, &viewInfo, nullptr, &m_gpuView));
        m_descInfo.imageView = m_gpuView;

        /* Since we're going to blit to the texture image, set its layout to
         * DESTINATION_OPTIMAL */
        SetImageLayout(ctx->m_loadCmdBuf, m_gpuTex, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_mips, m_layers);

        VkBufferImageCopy copyRegions[16] = {};
        size_t width = m_width;
        size_t height = m_height;
        size_t regionCount = std::min(size_t(16), m_mips);
        size_t offset = 0;
        for (int i=0 ; i<regionCount ; ++i)
        {
            size_t srcRowPitch = width * m_layers * m_pixelPitchNum / m_pixelPitchDenom;

            copyRegions[i].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegions[i].imageSubresource.mipLevel = i;
            copyRegions[i].imageSubresource.baseArrayLayer = 0;
            copyRegions[i].imageSubresource.layerCount = m_layers;
            copyRegions[i].imageExtent.width = width;
            copyRegions[i].imageExtent.height = height;
            copyRegions[i].imageExtent.depth = 1;
            copyRegions[i].bufferOffset = offset;

            if (width > 1)
                width /= 2;
            if (height > 1)
                height /= 2;
            offset += srcRowPitch;
        }

        /* Put the copy command into the command buffer */
        vk::CmdCopyBufferToImage(ctx->m_loadCmdBuf,
                                 m_cpuBuf,
                                 m_gpuTex,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 regionCount,
                                 copyRegions);

        /* Set the layout for the texture image from DESTINATION_OPTIMAL to
         * SHADER_READ_ONLY */
        SetImageLayout(ctx->m_loadCmdBuf, m_gpuTex, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_mips, m_layers);
    }

    TextureFormat format() const {return m_fmt;}
    size_t layers() const {return m_layers;}
};

class VulkanTextureD : public ITextureD
{
    friend class VulkanDataFactory;
    friend struct VulkanCommandQueue;
    size_t m_width;
    size_t m_height;
    TextureFormat m_fmt;
    VulkanCommandQueue* m_q;
    std::unique_ptr<uint8_t[]> m_stagingBuf;
    size_t m_cpuSz;
    VkDeviceSize m_srcRowPitch;
    VkDeviceSize m_cpuOffsets[2];
    VkFormat m_vkFmt;
    int m_validSlots = 0;
    VulkanTextureD(IGraphicsData* parent, VulkanCommandQueue* q, VulkanContext* ctx,
                   size_t width, size_t height, TextureFormat fmt)
    : ITextureD(parent), m_width(width), m_height(height), m_fmt(fmt), m_q(q)
    {
        VkFormat pfmt;
        switch (fmt)
        {
        case TextureFormat::RGBA8:
            pfmt = VK_FORMAT_R8G8B8A8_UNORM;
            m_srcRowPitch = width * 4;
            m_cpuSz = m_srcRowPitch * height;
            break;
        case TextureFormat::I8:
            pfmt = VK_FORMAT_R8_UNORM;
            m_srcRowPitch = width;
            m_cpuSz = m_srcRowPitch * height;
            break;
        default:
            Log.report(logvisor::Fatal, "unsupported tex format");
        }
        m_vkFmt = pfmt;
        m_stagingBuf.reset(new uint8_t[m_cpuSz]);

        /* create buffers */
        VkBufferCreateInfo bufCreateInfo = {};
        bufCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufCreateInfo.size = m_cpuSz;
        bufCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        /* compute size for host-mappable images */
        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAlloc.pNext = nullptr;
        memAlloc.memoryTypeIndex = 0;
        memAlloc.allocationSize = 0;
        uint32_t memTypeBits = ~0;
        for (int i=0 ; i<2 ; ++i)
        {
            m_cpuOffsets[i] = memAlloc.allocationSize;

            /* create cpu buffer */
            ThrowIfFailed(vk::CreateBuffer(ctx->m_dev, &bufCreateInfo, nullptr, &m_cpuBuf[i]));

            VkMemoryRequirements memReqs;
            vk::GetBufferMemoryRequirements(ctx->m_dev, m_cpuBuf[i], &memReqs);
            memAlloc.allocationSize += memReqs.size;
            memAlloc.allocationSize = (memAlloc.allocationSize + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
            memTypeBits &= memReqs.memoryTypeBits;

        }
        ThrowIfFalse(MemoryTypeFromProperties(ctx, memTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                              &memAlloc.memoryTypeIndex));

        /* allocate memory */
        ThrowIfFailed(vk::AllocateMemory(ctx->m_dev, &memAlloc, nullptr, &m_cpuMem));

        VkImageCreateInfo texCreateInfo = {};
        texCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        texCreateInfo.pNext = nullptr;
        texCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        texCreateInfo.format = pfmt;
        texCreateInfo.extent.width = width;
        texCreateInfo.extent.height = height;
        texCreateInfo.extent.depth = 1;
        texCreateInfo.mipLevels = 1;
        texCreateInfo.arrayLayers = 1;
        texCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        texCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        texCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        texCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        texCreateInfo.queueFamilyIndexCount = 0;
        texCreateInfo.pQueueFamilyIndices = nullptr;
        texCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        texCreateInfo.flags = 0;

        for (int i=0 ; i<2 ; ++i)
        {
            /* bind cpu memory */
            ThrowIfFailed(vk::BindBufferMemory(ctx->m_dev, m_cpuBuf[i], m_cpuMem, m_cpuOffsets[i]));

            /* create gpu image */
            ThrowIfFailed(vk::CreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_gpuTex[i]));

            m_descInfo[i].sampler = ctx->m_linearSampler;
            m_descInfo[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }
    void update(int b);
public:
    VkBuffer m_cpuBuf[2];
    VkDeviceMemory m_cpuMem;
    VkImage m_gpuTex[2];
    VkImageView m_gpuView[2];
    VkDeviceSize m_gpuOffset[2];
    VkDescriptorImageInfo m_descInfo[2];
    ~VulkanTextureD();

    void load(const void* data, size_t sz);
    void* map(size_t sz);
    void unmap();

    VkDeviceSize sizeForGPU(VulkanContext* ctx, uint32_t& memTypeBits, VkDeviceSize offset)
    {
        for (int i=0 ; i<2 ; ++i)
        {
            VkMemoryRequirements memReqs;
            vk::GetImageMemoryRequirements(ctx->m_dev, m_gpuTex[i], &memReqs);
            memTypeBits &= memReqs.memoryTypeBits;

            m_gpuOffset[i] = offset;
            offset += memReqs.size;
            offset = (offset + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
        }

        return offset;
    }

    void placeForGPU(VulkanContext* ctx, VkDeviceMemory mem)
    {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.pNext = nullptr;
        viewInfo.image = nullptr;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_vkFmt;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        for (int i=0 ; i<2 ; ++i)
        {
            /* bind memory */
            ThrowIfFailed(vk::BindImageMemory(ctx->m_dev, m_gpuTex[i], mem, m_gpuOffset[i]));

            /* create image view */
            viewInfo.image = m_gpuTex[i];
            ThrowIfFailed(vk::CreateImageView(ctx->m_dev, &viewInfo, nullptr, &m_gpuView[i]));

            m_descInfo[i].imageView = m_gpuView[i];
        }
    }

    TextureFormat format() const {return m_fmt;}
};

#define MAX_BIND_TEXS 4

class VulkanTextureR : public ITextureR
{
    friend class VulkanDataFactory;
    friend struct VulkanCommandQueue;
    size_t m_width = 0;
    size_t m_height = 0;
    size_t m_samples = 0;

    size_t m_colorBindCount;
    size_t m_depthBindCount;

    void Setup(VulkanContext* ctx, size_t width, size_t height, size_t samples,
               size_t colorBindCount, size_t depthBindCount)
    {
        /* no-ops on first call */
        doDestroy();
        m_layout = VK_IMAGE_LAYOUT_UNDEFINED;

        /* color target */
        VkImageCreateInfo texCreateInfo = {};
        texCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        texCreateInfo.pNext = nullptr;
        texCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        texCreateInfo.format = ctx->m_displayFormat;
        texCreateInfo.extent.width = width;
        texCreateInfo.extent.height = height;
        texCreateInfo.extent.depth = 1;
        texCreateInfo.mipLevels = 1;
        texCreateInfo.arrayLayers = 1;
        texCreateInfo.samples = VkSampleCountFlagBits(samples);
        texCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        texCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        texCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        texCreateInfo.queueFamilyIndexCount = 0;
        texCreateInfo.pQueueFamilyIndices = nullptr;
        texCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        texCreateInfo.flags = 0;
        ThrowIfFailed(vk::CreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_colorTex));

        /* depth target */
        texCreateInfo.format = VK_FORMAT_D24_UNORM_S8_UINT;
        texCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ThrowIfFailed(vk::CreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_depthTex));

        /* tally total memory requirements */
        VkMemoryRequirements memReqs;
        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAlloc.pNext = nullptr;
        memAlloc.memoryTypeIndex = 0;
        memAlloc.allocationSize = 0;
        uint32_t memTypeBits = ~0;

        VkDeviceSize gpuOffsets[2];
        VkDeviceSize colorOffsets[MAX_BIND_TEXS];
        VkDeviceSize depthOffsets[MAX_BIND_TEXS];

        vk::GetImageMemoryRequirements(ctx->m_dev, m_colorTex, &memReqs);
        gpuOffsets[0] = memAlloc.allocationSize;
        memAlloc.allocationSize += memReqs.size;
        memAlloc.allocationSize = (memAlloc.allocationSize + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
        memTypeBits &= memReqs.memoryTypeBits;

        vk::GetImageMemoryRequirements(ctx->m_dev, m_depthTex, &memReqs);
        gpuOffsets[1] = memAlloc.allocationSize;
        memAlloc.allocationSize += memReqs.size;
        memAlloc.allocationSize = (memAlloc.allocationSize + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
        memTypeBits &= memReqs.memoryTypeBits;

        for (size_t i=0 ; i<colorBindCount ; ++i)
        {
            m_colorBindLayout[i] = VK_IMAGE_LAYOUT_UNDEFINED;
            texCreateInfo.format = ctx->m_displayFormat;
            texCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            ThrowIfFailed(vk::CreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_colorBindTex[i]));

            vk::GetImageMemoryRequirements(ctx->m_dev, m_colorBindTex[i], &memReqs);
            colorOffsets[i] = memAlloc.allocationSize;
            memAlloc.allocationSize += memReqs.size;
            memAlloc.allocationSize = (memAlloc.allocationSize + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
            memTypeBits &= memReqs.memoryTypeBits;

            m_colorBindDescInfo[i].sampler = ctx->m_linearSampler;
            m_colorBindDescInfo[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        for (size_t i=0 ; i<depthBindCount ; ++i)
        {
            m_depthBindLayout[i] = VK_IMAGE_LAYOUT_UNDEFINED;
            texCreateInfo.format = VK_FORMAT_D24_UNORM_S8_UINT;
            texCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            ThrowIfFailed(vk::CreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_depthBindTex[i]));

            vk::GetImageMemoryRequirements(ctx->m_dev, m_depthBindTex[i], &memReqs);
            depthOffsets[i] = memAlloc.allocationSize;
            memAlloc.allocationSize += memReqs.size;
            memAlloc.allocationSize = (memAlloc.allocationSize + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
            memTypeBits &= memReqs.memoryTypeBits;

            m_depthBindDescInfo[i].sampler = ctx->m_linearSampler;
            m_depthBindDescInfo[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        ThrowIfFalse(MemoryTypeFromProperties(ctx, memTypeBits, 0, &memAlloc.memoryTypeIndex));

        /* allocate memory */
        ThrowIfFailed(vk::AllocateMemory(ctx->m_dev, &memAlloc, nullptr, &m_gpuMem));

        uint8_t* mappedData;
        ThrowIfFailed(vk::MapMemory(ctx->m_dev, m_gpuMem, 0, memAlloc.allocationSize, 0, reinterpret_cast<void**>(&mappedData)));
        memset(mappedData, 0, memAlloc.allocationSize);
        vk::UnmapMemory(ctx->m_dev, m_gpuMem);

        /* bind memory */
        ThrowIfFailed(vk::BindImageMemory(ctx->m_dev, m_colorTex, m_gpuMem, gpuOffsets[0]));
        ThrowIfFailed(vk::BindImageMemory(ctx->m_dev, m_depthTex, m_gpuMem, gpuOffsets[1]));

        /* Create resource views */
        VkImageViewCreateInfo viewCreateInfo = {};
        viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCreateInfo.pNext = nullptr;
        viewCreateInfo.image = m_colorTex;
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCreateInfo.format = ctx->m_displayFormat;
        viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCreateInfo.subresourceRange.baseMipLevel = 0;
        viewCreateInfo.subresourceRange.levelCount = 1;
        viewCreateInfo.subresourceRange.baseArrayLayer = 0;
        viewCreateInfo.subresourceRange.layerCount = 1;
        ThrowIfFailed(vk::CreateImageView(ctx->m_dev, &viewCreateInfo, nullptr, &m_colorView));

        viewCreateInfo.image = m_depthTex;
        viewCreateInfo.format = VK_FORMAT_D24_UNORM_S8_UINT;
        viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        ThrowIfFailed(vk::CreateImageView(ctx->m_dev, &viewCreateInfo, nullptr, &m_depthView));

        for (size_t i=0 ; i<colorBindCount ; ++i)
        {
            ThrowIfFailed(vk::BindImageMemory(ctx->m_dev, m_colorBindTex[i], m_gpuMem, colorOffsets[i]));
            viewCreateInfo.image = m_colorBindTex[i];
            viewCreateInfo.format = ctx->m_displayFormat;
            viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ThrowIfFailed(vk::CreateImageView(ctx->m_dev, &viewCreateInfo, nullptr, &m_colorBindView[i]));
            m_colorBindDescInfo[i].imageView = m_colorBindView[i];
        }

        for (size_t i=0 ; i<depthBindCount ; ++i)
        {
            ThrowIfFailed(vk::BindImageMemory(ctx->m_dev, m_depthBindTex[i], m_gpuMem, depthOffsets[i]));
            viewCreateInfo.image = m_depthBindTex[i];
            viewCreateInfo.format = VK_FORMAT_D24_UNORM_S8_UINT;
            viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            ThrowIfFailed(vk::CreateImageView(ctx->m_dev, &viewCreateInfo, nullptr, &m_depthBindView[i]));
            m_depthBindDescInfo[i].imageView = m_depthBindView[i];
        }

        /* framebuffer */
        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.pNext = nullptr;
        fbCreateInfo.renderPass = ctx->m_pass;
        fbCreateInfo.attachmentCount = 2;
        fbCreateInfo.width = width;
        fbCreateInfo.height = height;
        fbCreateInfo.layers = 1;
        VkImageView attachments[2] = {m_colorView, m_depthView};
        fbCreateInfo.pAttachments = attachments;
        ThrowIfFailed(vk::CreateFramebuffer(ctx->m_dev, &fbCreateInfo, nullptr, &m_framebuffer));

        m_passBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        m_passBeginInfo.pNext = nullptr;
        m_passBeginInfo.renderPass = ctx->m_pass;
        m_passBeginInfo.framebuffer = m_framebuffer;
        m_passBeginInfo.renderArea.offset.x = 0;
        m_passBeginInfo.renderArea.offset.y = 0;
        m_passBeginInfo.renderArea.extent.width = width;
        m_passBeginInfo.renderArea.extent.height = height;
        m_passBeginInfo.clearValueCount = 0;
        m_passBeginInfo.pClearValues = nullptr;
    }

    VulkanCommandQueue* m_q;
    VulkanTextureR(IGraphicsData* parent, VulkanContext* ctx, VulkanCommandQueue* q,
                   size_t width, size_t height, size_t samples,
                   size_t colorBindCount, size_t depthBindCount)
    : ITextureR(parent), m_q(q), m_width(width), m_height(height), m_samples(samples),
      m_colorBindCount(colorBindCount),
      m_depthBindCount(depthBindCount)
    {
        if (colorBindCount > MAX_BIND_TEXS)
            Log.report(logvisor::Fatal, "too many color bindings for render texture");
        if (depthBindCount > MAX_BIND_TEXS)
            Log.report(logvisor::Fatal, "too many depth bindings for render texture");

        if (samples == 0) m_samples = 1;
        Setup(ctx, width, height, samples, colorBindCount, depthBindCount);
    }
public:
    size_t samples() const {return m_samples;}
    VkDeviceMemory m_gpuMem = VK_NULL_HANDLE;

    VkImage m_colorTex = VK_NULL_HANDLE;
    VkImageView m_colorView = VK_NULL_HANDLE;

    VkImage m_depthTex = VK_NULL_HANDLE;
    VkImageView m_depthView = VK_NULL_HANDLE;

    VkImage m_colorBindTex[MAX_BIND_TEXS] = {};
    VkImageView m_colorBindView[MAX_BIND_TEXS] = {};
    VkDescriptorImageInfo m_colorBindDescInfo[MAX_BIND_TEXS] = {};

    VkImage m_depthBindTex[MAX_BIND_TEXS] = {};
    VkImageView m_depthBindView[MAX_BIND_TEXS] = {};
    VkDescriptorImageInfo m_depthBindDescInfo[MAX_BIND_TEXS] = {};

    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkRenderPassBeginInfo m_passBeginInfo = {};

    VkImageLayout m_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout m_colorBindLayout[MAX_BIND_TEXS] = {};
    VkImageLayout m_depthBindLayout[MAX_BIND_TEXS] = {};

    void doDestroy();
    ~VulkanTextureR();

    void resize(VulkanContext* ctx, size_t width, size_t height)
    {
        if (width < 1)
            width = 1;
        if (height < 1)
            height = 1;
        m_width = width;
        m_height = height;
        Setup(ctx, width, height, m_samples, m_colorBindCount, m_depthBindCount);
    }
};

static const size_t SEMANTIC_SIZE_TABLE[] =
{
    0,
    12,
    16,
    12,
    16,
    16,
    4,
    8,
    16,
    16,
    16
};

static const VkFormat SEMANTIC_TYPE_TABLE[] =
{
    VK_FORMAT_UNDEFINED,
    VK_FORMAT_R32G32B32_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R32G32B32_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_R32G32_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT
};

struct VulkanVertexFormat : IVertexFormat
{
    VkVertexInputBindingDescription m_bindings[2];
    std::unique_ptr<VkVertexInputAttributeDescription[]> m_attributes;
    VkPipelineVertexInputStateCreateInfo m_info;
    size_t m_stride = 0;
    size_t m_instStride = 0;

    VulkanVertexFormat(IGraphicsData* parent, size_t elementCount,
                       const VertexElementDescriptor* elements)
    : IVertexFormat(parent), m_attributes(new VkVertexInputAttributeDescription[elementCount])
    {
        m_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        m_info.pNext = nullptr;
        m_info.flags = 0;
        m_info.vertexBindingDescriptionCount = 0;
        m_info.pVertexBindingDescriptions = m_bindings;
        m_info.vertexAttributeDescriptionCount = elementCount;
        m_info.pVertexAttributeDescriptions = m_attributes.get();

        for (size_t i=0 ; i<elementCount ; ++i)
        {
            const VertexElementDescriptor* elemin = &elements[i];
            VkVertexInputAttributeDescription& attribute = m_attributes[i];
            int semantic = int(elemin->semantic & boo::VertexSemantic::SemanticMask);
            attribute.location = i;
            attribute.format = SEMANTIC_TYPE_TABLE[semantic];
            if ((elemin->semantic & boo::VertexSemantic::Instanced) != boo::VertexSemantic::None)
            {
                attribute.binding = 1;
                attribute.offset = m_instStride;
                m_instStride += SEMANTIC_SIZE_TABLE[semantic];
            }
            else
            {
                attribute.binding = 0;
                attribute.offset = m_stride;
                m_stride += SEMANTIC_SIZE_TABLE[semantic];
            }
        }

        if (m_stride)
        {
            m_bindings[0].binding = 0;
            m_bindings[0].stride = m_stride;
            m_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            ++m_info.vertexBindingDescriptionCount;
        }
        if (m_instStride)
        {
            m_bindings[m_info.vertexBindingDescriptionCount].binding = 1;
            m_bindings[m_info.vertexBindingDescriptionCount].stride = m_instStride;
            m_bindings[m_info.vertexBindingDescriptionCount].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
            ++m_info.vertexBindingDescriptionCount;
        }
    }
};

static const VkPrimitiveTopology PRIMITIVE_TABLE[] =
{
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
};

static const VkBlendFactor BLEND_FACTOR_TABLE[] =
{
    VK_BLEND_FACTOR_ZERO,
    VK_BLEND_FACTOR_ONE,
    VK_BLEND_FACTOR_SRC_COLOR,
    VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    VK_BLEND_FACTOR_DST_COLOR,
    VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    VK_BLEND_FACTOR_SRC_ALPHA,
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    VK_BLEND_FACTOR_DST_ALPHA,
    VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
    VK_BLEND_FACTOR_SRC1_COLOR,
    VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR
};

class VulkanShaderPipeline : public IShaderPipeline
{
    friend class VulkanDataFactory;
    friend struct VulkanShaderDataBinding;
    VulkanContext* m_ctx;
    VkPipelineCache m_pipelineCache;
    const VulkanVertexFormat* m_vtxFmt;
    VulkanShareableShader::Token m_vert;
    VulkanShareableShader::Token m_frag;
    VulkanShaderPipeline(IGraphicsData* parent,
                         VulkanContext* ctx,
                         VulkanShareableShader::Token&& vert,
                         VulkanShareableShader::Token&& frag,
                         VkPipelineCache pipelineCache,
                         const VulkanVertexFormat* vtxFmt,
                         BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                         ZTest depthTest, bool depthWrite, bool colorWrite,
                         bool alphaWrite, CullMode culling)
    : IShaderPipeline(parent), m_ctx(ctx), m_pipelineCache(pipelineCache), m_vtxFmt(vtxFmt),
      m_vert(std::move(vert)), m_frag(std::move(frag))
    {
        VkCullModeFlagBits cullMode;
        switch (culling)
        {
        case CullMode::None:
        default:
            cullMode = VK_CULL_MODE_NONE;
            break;
        case CullMode::Backface:
            cullMode = VK_CULL_MODE_BACK_BIT;
            break;
        case CullMode::Frontface:
            cullMode = VK_CULL_MODE_FRONT_BIT;
            break;
        }

        VkDynamicState dynamicStateEnables[VK_DYNAMIC_STATE_RANGE_SIZE] = {};
        VkPipelineDynamicStateCreateInfo dynamicState = {};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.pNext = nullptr;
        dynamicState.pDynamicStates = dynamicStateEnables;
        dynamicState.dynamicStateCount = 0;

        VkPipelineShaderStageCreateInfo stages[2] = {};

        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].pNext = nullptr;
        stages[0].flags = 0;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = m_vert.get().m_shader;
        stages[0].pName = "main";
        stages[0].pSpecializationInfo = nullptr;

        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].pNext = nullptr;
        stages[1].flags = 0;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = m_frag.get().m_shader;
        stages[1].pName = "main";
        stages[1].pSpecializationInfo = nullptr;

        VkPipelineInputAssemblyStateCreateInfo assemblyInfo = {};
        assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assemblyInfo.pNext = nullptr;
        assemblyInfo.flags = 0;
        assemblyInfo.topology = PRIMITIVE_TABLE[int(prim)];
        assemblyInfo.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportInfo = {};
        viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportInfo.pNext = nullptr;
        viewportInfo.flags = 0;
        viewportInfo.viewportCount = 1;
        viewportInfo.pViewports = nullptr;
        viewportInfo.scissorCount = 1;
        viewportInfo.pScissors = nullptr;
        dynamicStateEnables[dynamicState.dynamicStateCount++] = VK_DYNAMIC_STATE_VIEWPORT;
        dynamicStateEnables[dynamicState.dynamicStateCount++] = VK_DYNAMIC_STATE_SCISSOR;

        VkPipelineRasterizationStateCreateInfo rasterizationInfo = {};
        rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationInfo.pNext = nullptr;
        rasterizationInfo.flags = 0;
        rasterizationInfo.depthClampEnable = VK_FALSE;
        rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;
        rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizationInfo.cullMode = cullMode;
        rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizationInfo.depthBiasEnable = VK_FALSE;
        rasterizationInfo.lineWidth = 1.f;

        VkPipelineMultisampleStateCreateInfo multisampleInfo = {};
        multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleInfo.pNext = nullptr;
        multisampleInfo.flags = 0;
        multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {};
        depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilInfo.pNext = nullptr;
        depthStencilInfo.flags = 0;
        depthStencilInfo.depthTestEnable = depthTest != ZTest::None;
        depthStencilInfo.depthWriteEnable = depthWrite;
        depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
        depthStencilInfo.back.compareOp = VK_COMPARE_OP_ALWAYS;

        switch (depthTest)
        {
        case ZTest::None:
        default:
            depthStencilInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
            break;
        case ZTest::LEqual:
            depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            break;
        case ZTest::Greater:
            depthStencilInfo.depthCompareOp = VK_COMPARE_OP_GREATER;
            break;
        case ZTest::Equal:
            depthStencilInfo.depthCompareOp = VK_COMPARE_OP_EQUAL;
            break;
        }

        VkPipelineColorBlendAttachmentState colorAttachment = {};
        colorAttachment.blendEnable = dstFac != BlendFactor::Zero;
        colorAttachment.srcColorBlendFactor = BLEND_FACTOR_TABLE[int(srcFac)];
        colorAttachment.dstColorBlendFactor = BLEND_FACTOR_TABLE[int(dstFac)];
        colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        colorAttachment.colorWriteMask =
                (colorWrite ? (VK_COLOR_COMPONENT_R_BIT |
                               VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT) : 0) |
                (alphaWrite ? VK_COLOR_COMPONENT_A_BIT : 0);

        VkPipelineColorBlendStateCreateInfo colorBlendInfo = {};
        colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendInfo.pNext = nullptr;
        colorBlendInfo.flags = 0;
        colorBlendInfo.logicOpEnable = VK_FALSE;
        colorBlendInfo.attachmentCount = 1;
        colorBlendInfo.pAttachments = &colorAttachment;

        VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.pNext = nullptr;
        pipelineCreateInfo.flags = 0;
        pipelineCreateInfo.stageCount = 2;
        pipelineCreateInfo.pStages = stages;
        pipelineCreateInfo.pVertexInputState = &vtxFmt->m_info;
        pipelineCreateInfo.pInputAssemblyState = &assemblyInfo;
        pipelineCreateInfo.pViewportState = &viewportInfo;
        pipelineCreateInfo.pRasterizationState = &rasterizationInfo;
        pipelineCreateInfo.pMultisampleState = &multisampleInfo;
        pipelineCreateInfo.pDepthStencilState = &depthStencilInfo;
        pipelineCreateInfo.pColorBlendState = &colorBlendInfo;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.layout = ctx->m_pipelinelayout;
        pipelineCreateInfo.renderPass = ctx->m_pass;

        ThrowIfFailed(vk::CreateGraphicsPipelines(ctx->m_dev, pipelineCache, 1, &pipelineCreateInfo,
                                                  nullptr, &m_pipeline));
    }
public:
    VkPipeline m_pipeline;
    ~VulkanShaderPipeline()
    {
        vk::DestroyPipeline(m_ctx->m_dev, m_pipeline, nullptr);
        if (m_pipelineCache)
            vk::DestroyPipelineCache(m_ctx->m_dev, m_pipelineCache, nullptr);
    }
    VulkanShaderPipeline& operator=(const VulkanShaderPipeline&) = delete;
    VulkanShaderPipeline(const VulkanShaderPipeline&) = delete;
};

static VkDeviceSize SizeBufferForGPU(IGraphicsBuffer* buf, VulkanContext* ctx,
                                     uint32_t& memTypeBits, VkDeviceSize offset)
{
    if (buf->dynamic())
        return static_cast<VulkanGraphicsBufferD*>(buf)->sizeForGPU(ctx, memTypeBits, offset);
    else
        return static_cast<VulkanGraphicsBufferS*>(buf)->sizeForGPU(ctx, memTypeBits, offset);
}

static VkDeviceSize SizeTextureForGPU(ITexture* tex, VulkanContext* ctx,
                                      uint32_t& memTypeBits, VkDeviceSize offset)
{
    switch (tex->type())
    {
    case TextureType::Dynamic:
        return static_cast<VulkanTextureD*>(tex)->sizeForGPU(ctx, memTypeBits, offset);
    case TextureType::Static:
        return static_cast<VulkanTextureS*>(tex)->sizeForGPU(ctx, memTypeBits, offset);
    case TextureType::StaticArray:
        return static_cast<VulkanTextureSA*>(tex)->sizeForGPU(ctx, memTypeBits, offset);
    default: break;
    }
    return offset;
}

static void PlaceTextureForGPU(ITexture* tex, VulkanContext* ctx, VkDeviceMemory mem)
{
    switch (tex->type())
    {
    case TextureType::Dynamic:
        static_cast<VulkanTextureD*>(tex)->placeForGPU(ctx, mem);
        break;
    case TextureType::Static:
        static_cast<VulkanTextureS*>(tex)->placeForGPU(ctx, mem);
        break;
    case TextureType::StaticArray:
        static_cast<VulkanTextureSA*>(tex)->placeForGPU(ctx, mem);
        break;
    default: break;
    }
}

static const VkDescriptorBufferInfo* GetBufferGPUResource(const IGraphicsBuffer* buf, int idx)
{
    if (buf->dynamic())
    {
        const VulkanGraphicsBufferD* cbuf = static_cast<const VulkanGraphicsBufferD*>(buf);
        return &cbuf->m_bufferInfo[idx];
    }
    else
    {
        const VulkanGraphicsBufferS* cbuf = static_cast<const VulkanGraphicsBufferS*>(buf);
        return &cbuf->m_bufferInfo;
    }
}

static const VkDescriptorImageInfo* GetTextureGPUResource(const ITexture* tex, int idx, int bindIdx, bool depth)
{
    switch (tex->type())
    {
    case TextureType::Dynamic:
    {
        const VulkanTextureD* ctex = static_cast<const VulkanTextureD*>(tex);
        return &ctex->m_descInfo[idx];
    }
    case TextureType::Static:
    {
        const VulkanTextureS* ctex = static_cast<const VulkanTextureS*>(tex);
        return &ctex->m_descInfo;
    }
    case TextureType::StaticArray:
    {
        const VulkanTextureSA* ctex = static_cast<const VulkanTextureSA*>(tex);
        return &ctex->m_descInfo;
    }
    case TextureType::Render:
    {
        const VulkanTextureR* ctex = static_cast<const VulkanTextureR*>(tex);
        return depth ? &ctex->m_depthBindDescInfo[bindIdx] : &ctex->m_colorBindDescInfo[bindIdx];
    }
    default: break;
    }
    return nullptr;
}

struct VulkanShaderDataBinding : IShaderDataBindingPriv
{
    VulkanContext* m_ctx;
    VulkanShaderPipeline* m_pipeline;
    IGraphicsBuffer* m_vbuf;
    IGraphicsBuffer* m_instVbuf;
    IGraphicsBuffer* m_ibuf;
    size_t m_ubufCount;
    std::unique_ptr<IGraphicsBuffer*[]> m_ubufs;
    std::vector<std::array<VkDescriptorBufferInfo, 2>> m_ubufOffs;
    size_t m_texCount;
    VkImageView m_knownViewHandles[2][8] = {};
    struct BindTex
    {
        ITexture* tex;
        int idx;
        bool depth;
    };
    std::unique_ptr<BindTex[]> m_texs;

    VkBuffer m_vboBufs[2][2] = {{},{}};
    VkDeviceSize m_vboOffs[2][2] = {{},{}};
    VkBuffer m_iboBufs[2] = {};
    VkDeviceSize m_iboOffs[2] = {};

    VkDescriptorPool m_descPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descSets[2] = {};

    size_t m_vertOffset;
    size_t m_instOffset;

#ifndef NDEBUG
    /* Debugging aids */
    bool m_committed = false;
#endif

    VulkanShaderDataBinding(VulkanData* d,
                            VulkanContext* ctx,
                            IShaderPipeline* pipeline,
                            IGraphicsBuffer* vbuf, IGraphicsBuffer* instVbuf, IGraphicsBuffer* ibuf,
                            size_t ubufCount, IGraphicsBuffer** ubufs,
                            const size_t* ubufOffs, const size_t* ubufSizes,
                            size_t texCount, ITexture** texs,
                            const int* bindIdxs, const bool* depthBinds,
                            size_t baseVert, size_t baseInst)
    : IShaderDataBindingPriv(d),
      m_ctx(ctx),
      m_pipeline(static_cast<VulkanShaderPipeline*>(pipeline)),
      m_vbuf(vbuf),
      m_instVbuf(instVbuf),
      m_ibuf(ibuf),
      m_ubufCount(ubufCount),
      m_ubufs(new IGraphicsBuffer*[ubufCount]),
      m_texCount(texCount),
      m_texs(new BindTex[texCount])
    {
        m_vertOffset = baseVert * m_pipeline->m_vtxFmt->m_stride;
        m_instOffset = baseInst * m_pipeline->m_vtxFmt->m_instStride;

        if (ubufOffs && ubufSizes)
        {
            m_ubufOffs.reserve(ubufCount);
            for (size_t i=0 ; i<ubufCount ; ++i)
            {
#ifndef NDEBUG
                if (ubufOffs[i] % 256)
                    Log.report(logvisor::Fatal, "non-256-byte-aligned uniform-offset %d provided to newShaderDataBinding", int(i));
#endif
                std::array<VkDescriptorBufferInfo, 2> fillArr;
                fillArr.fill({VK_NULL_HANDLE, ubufOffs[i], (ubufSizes[i] + 255) & ~255});
                m_ubufOffs.push_back(fillArr);
            }
        }
        for (size_t i=0 ; i<ubufCount ; ++i)
        {
#ifndef NDEBUG
            if (!ubufs[i])
                Log.report(logvisor::Fatal, "null uniform-buffer %d provided to newShaderDataBinding", int(i));
#endif
            m_ubufs[i] = ubufs[i];
        }
        for (size_t i=0 ; i<texCount ; ++i)
        {
            m_texs[i].tex = texs[i];
            m_texs[i].idx = bindIdxs ? bindIdxs[i] : 0;
            m_texs[i].depth = depthBinds ? depthBinds[i] : 0;
        }

        size_t totalDescs = ubufCount + texCount;
        if (totalDescs > 0)
        {
            VkDescriptorPoolSize poolSizes[2] = {};
            VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
            descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descriptorPoolInfo.pNext = nullptr;
            descriptorPoolInfo.maxSets = 2;
            descriptorPoolInfo.poolSizeCount = 2;
            descriptorPoolInfo.pPoolSizes = poolSizes;

            poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            poolSizes[0].descriptorCount = BOO_GLSL_MAX_UNIFORM_COUNT * 2;

            poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            poolSizes[1].descriptorCount = BOO_GLSL_MAX_TEXTURE_COUNT * 2;

            ThrowIfFailed(vk::CreateDescriptorPool(ctx->m_dev, &descriptorPoolInfo, nullptr, &m_descPool));

            VkDescriptorSetLayout layouts[] = {ctx->m_descSetLayout, ctx->m_descSetLayout};
            VkDescriptorSetAllocateInfo descAllocInfo;
            descAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descAllocInfo.pNext = nullptr;
            descAllocInfo.descriptorPool = m_descPool;
            descAllocInfo.descriptorSetCount = 2;
            descAllocInfo.pSetLayouts = layouts;
            ThrowIfFailed(vk::AllocateDescriptorSets(ctx->m_dev, &descAllocInfo, m_descSets));
        }
    }

    ~VulkanShaderDataBinding()
    {
        vk::DestroyDescriptorPool(m_ctx->m_dev, m_descPool, nullptr);
    }

    void commit(VulkanContext* ctx)
    {        
        VkWriteDescriptorSet writes[(BOO_GLSL_MAX_UNIFORM_COUNT + BOO_GLSL_MAX_TEXTURE_COUNT) * 2] = {};
        size_t totalWrites = 0;
        for (int b=0 ; b<2 ; ++b)
        {
            if (m_vbuf)
            {
                const VkDescriptorBufferInfo* vbufInfo = GetBufferGPUResource(m_vbuf, b);
                m_vboBufs[b][0] = vbufInfo->buffer;
                m_vboOffs[b][0] = vbufInfo->offset + m_vertOffset;
            }
            if (m_instVbuf)
            {
                const VkDescriptorBufferInfo* vbufInfo = GetBufferGPUResource(m_instVbuf, b);
                m_vboBufs[b][1] = vbufInfo->buffer;
                m_vboOffs[b][1] = vbufInfo->offset + m_instOffset;
            }
            if (m_ibuf)
            {
                const VkDescriptorBufferInfo* ibufInfo = GetBufferGPUResource(m_ibuf, b);
                m_iboBufs[b] = ibufInfo->buffer;
                m_iboOffs[b] = ibufInfo->offset;
            }

            size_t binding = 0;
            if (m_ubufOffs.size())
            {
                for (size_t i=0 ; i<BOO_GLSL_MAX_UNIFORM_COUNT ; ++i)
                {
                    if (i<m_ubufCount)
                    {
                        VkDescriptorBufferInfo& modInfo = m_ubufOffs[i][b];
                        if (modInfo.range)
                        {
                            writes[totalWrites].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                            writes[totalWrites].pNext = nullptr;
                            writes[totalWrites].dstSet = m_descSets[b];
                            writes[totalWrites].descriptorCount = 1;
                            writes[totalWrites].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                            const VkDescriptorBufferInfo* origInfo = GetBufferGPUResource(m_ubufs[i], b);
                            modInfo.buffer = origInfo->buffer;
                            modInfo.offset += origInfo->offset;
                            writes[totalWrites].pBufferInfo = &modInfo;
                            writes[totalWrites].dstArrayElement = 0;
                            writes[totalWrites].dstBinding = binding;
                            ++totalWrites;
                        }
                    }
                    ++binding;
                }
            }
            else
            {
                for (size_t i=0 ; i<BOO_GLSL_MAX_UNIFORM_COUNT ; ++i)
                {
                    if (i<m_ubufCount)
                    {
                        writes[totalWrites].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        writes[totalWrites].pNext = nullptr;
                        writes[totalWrites].dstSet = m_descSets[b];
                        writes[totalWrites].descriptorCount = 1;
                        writes[totalWrites].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                        writes[totalWrites].pBufferInfo = GetBufferGPUResource(m_ubufs[i], b);
                        writes[totalWrites].dstArrayElement = 0;
                        writes[totalWrites].dstBinding = binding;
                        ++totalWrites;
                    }
                    ++binding;
                }
            }

            for (size_t i=0 ; i<BOO_GLSL_MAX_TEXTURE_COUNT ; ++i)
            {
                if (i<m_texCount && m_texs[i].tex)
                {
                    writes[totalWrites].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[totalWrites].pNext = nullptr;
                    writes[totalWrites].dstSet = m_descSets[b];
                    writes[totalWrites].descriptorCount = 1;
                    writes[totalWrites].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    writes[totalWrites].pImageInfo = GetTextureGPUResource(m_texs[i].tex, b, m_texs[i].idx, m_texs[i].depth);
                    writes[totalWrites].dstArrayElement = 0;
                    writes[totalWrites].dstBinding = binding;
                    m_knownViewHandles[b][i] = writes[totalWrites].pImageInfo->imageView;
                    ++totalWrites;
                }
                ++binding;
            }
        }
        if (totalWrites)
            vk::UpdateDescriptorSets(ctx->m_dev, totalWrites, writes, 0, nullptr);

#ifndef NDEBUG
        m_committed = true;
#endif
    }

    void bind(VkCommandBuffer cmdBuf, int b)
    {
#ifndef NDEBUG
        if (!m_committed)
            Log.report(logvisor::Fatal,
                       "attempted to use uncommitted VulkanShaderDataBinding");
#endif

        /* Ensure resized texture bindings are re-bound */
        size_t binding = BOO_GLSL_MAX_UNIFORM_COUNT;
        VkWriteDescriptorSet writes[BOO_GLSL_MAX_TEXTURE_COUNT] = {};
        size_t totalWrites = 0;
        for (size_t i=0 ; i<BOO_GLSL_MAX_TEXTURE_COUNT ; ++i)
        {
            if (i<m_texCount && m_texs[i].tex)
            {
                const VkDescriptorImageInfo* resComp = GetTextureGPUResource(m_texs[i].tex, b, m_texs[i].idx, m_texs[i].depth);
                if (resComp->imageView != m_knownViewHandles[b][i])
                {
                    writes[totalWrites].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[totalWrites].pNext = nullptr;
                    writes[totalWrites].dstSet = m_descSets[b];
                    writes[totalWrites].descriptorCount = 1;
                    writes[totalWrites].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    writes[totalWrites].pImageInfo = resComp;
                    writes[totalWrites].dstArrayElement = 0;
                    writes[totalWrites].dstBinding = binding;
                    ++totalWrites;
                    m_knownViewHandles[b][i] = resComp->imageView;
                }
            }
            ++binding;
        }
        if (totalWrites)
            vk::UpdateDescriptorSets(m_ctx->m_dev, totalWrites, writes, 0, nullptr);

        vk::CmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->m_pipeline);
        if (m_descSets[b])
            vk::CmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ctx->m_pipelinelayout,
                                      0, 1, &m_descSets[b], 0, nullptr);

        if (m_vbuf && m_instVbuf)
            vk::CmdBindVertexBuffers(cmdBuf, 0, 2, m_vboBufs[b], m_vboOffs[b]);
        else if (m_vbuf)
            vk::CmdBindVertexBuffers(cmdBuf, 0, 1, m_vboBufs[b], m_vboOffs[b]);
        else if (m_instVbuf)
            vk::CmdBindVertexBuffers(cmdBuf, 1, 1, &m_vboBufs[b][1], &m_vboOffs[b][1]);

        if (m_ibuf)
            vk::CmdBindIndexBuffer(cmdBuf, m_iboBufs[b], m_iboOffs[b], VK_INDEX_TYPE_UINT32);
    }
};

struct VulkanCommandQueue : IGraphicsCommandQueue
{
    Platform platform() const {return IGraphicsDataFactory::Platform::Vulkan;}
    const SystemChar* platformName() const {return _S("Vulkan");}
    VulkanContext* m_ctx;
    VulkanContext::Window* m_windowCtx;
    IGraphicsContext* m_parent;

    VkCommandPool m_cmdPool;
    VkCommandBuffer m_cmdBufs[2];
    VkSemaphore m_swapChainReadySem = VK_NULL_HANDLE;
    VkSemaphore m_drawCompleteSem = VK_NULL_HANDLE;
    VkFence m_drawCompleteFence;

    VkCommandPool m_dynamicCmdPool;
    VkCommandBuffer m_dynamicCmdBufs[2];
    VkFence m_dynamicBufFence;

    bool m_running = true;
    bool m_dynamicNeedsReset = false;
    bool m_submitted = false;

    size_t m_fillBuf = 0;
    size_t m_drawBuf = 0;

    void resetCommandBuffer()
    {
        ThrowIfFailed(vk::ResetCommandBuffer(m_cmdBufs[m_fillBuf], 0));
        VkCommandBufferBeginInfo cmdBufBeginInfo = {};
        cmdBufBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBufBeginInfo.flags = 0;
        ThrowIfFailed(vk::BeginCommandBuffer(m_cmdBufs[m_fillBuf], &cmdBufBeginInfo));
    }

    void resetDynamicCommandBuffer()
    {
        ThrowIfFailed(vk::ResetCommandBuffer(m_dynamicCmdBufs[m_fillBuf], 0));
        VkCommandBufferBeginInfo cmdBufBeginInfo = {};
        cmdBufBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBufBeginInfo.flags = 0;
        ThrowIfFailed(vk::BeginCommandBuffer(m_dynamicCmdBufs[m_fillBuf], &cmdBufBeginInfo));
        m_dynamicNeedsReset = false;
    }

    void stallDynamicUpload()
    {
        if (m_dynamicNeedsReset)
        {
            ThrowIfFailed(vk::WaitForFences(m_ctx->m_dev, 1, &m_dynamicBufFence, VK_FALSE, -1));
            resetDynamicCommandBuffer();
        }
    }

    VulkanCommandQueue(VulkanContext* ctx, VulkanContext::Window* windowCtx, IGraphicsContext* parent)
    : m_ctx(ctx), m_windowCtx(windowCtx), m_parent(parent)
    {
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_ctx->m_graphicsQueueFamilyIndex;
        ThrowIfFailed(vk::CreateCommandPool(ctx->m_dev, &poolInfo, nullptr, &m_cmdPool));
        ThrowIfFailed(vk::CreateCommandPool(ctx->m_dev, &poolInfo, nullptr, &m_dynamicCmdPool));

        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_cmdPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 2;

        VkCommandBufferBeginInfo cmdBufBeginInfo = {};
        cmdBufBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBufBeginInfo.flags = 0;

        ThrowIfFailed(vk::AllocateCommandBuffers(m_ctx->m_dev, &allocInfo, m_cmdBufs));
        ThrowIfFailed(vk::BeginCommandBuffer(m_cmdBufs[0], &cmdBufBeginInfo));

        allocInfo.commandPool = m_dynamicCmdPool;
        ThrowIfFailed(vk::AllocateCommandBuffers(m_ctx->m_dev, &allocInfo, m_dynamicCmdBufs));
        ThrowIfFailed(vk::BeginCommandBuffer(m_dynamicCmdBufs[0], &cmdBufBeginInfo));

        VkSemaphoreCreateInfo semInfo = {};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        ThrowIfFailed(vk::CreateSemaphore(ctx->m_dev, &semInfo, nullptr, &m_swapChainReadySem));
        ThrowIfFailed(vk::CreateSemaphore(ctx->m_dev, &semInfo, nullptr, &m_drawCompleteSem));

        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        ThrowIfFailed(vk::CreateFence(m_ctx->m_dev, &fenceInfo, nullptr, &m_drawCompleteFence));
        ThrowIfFailed(vk::CreateFence(m_ctx->m_dev, &fenceInfo, nullptr, &m_dynamicBufFence));
    }

    void stopRenderer()
    {
        m_running = false;
        vk::WaitForFences(m_ctx->m_dev, 1, &m_drawCompleteFence, VK_FALSE, -1);
    }

    ~VulkanCommandQueue()
    {
        if (m_running)
            stopRenderer();

        vk::DestroyFence(m_ctx->m_dev, m_dynamicBufFence, nullptr);
        vk::DestroyFence(m_ctx->m_dev, m_drawCompleteFence, nullptr);
        vk::DestroySemaphore(m_ctx->m_dev, m_drawCompleteSem, nullptr);
        vk::DestroySemaphore(m_ctx->m_dev, m_swapChainReadySem, nullptr);
        vk::DestroyCommandPool(m_ctx->m_dev, m_dynamicCmdPool, nullptr);
        vk::DestroyCommandPool(m_ctx->m_dev, m_cmdPool, nullptr);
    }

    void setShaderDataBinding(IShaderDataBinding* binding)
    {
        VulkanShaderDataBinding* cbind = static_cast<VulkanShaderDataBinding*>(binding);
        cbind->bind(m_cmdBufs[m_fillBuf], m_fillBuf);
    }

    VulkanTextureR* m_boundTarget = nullptr;
    void setRenderTarget(ITextureR* target)
    {
        VulkanTextureR* ctarget = static_cast<VulkanTextureR*>(target);
        VkCommandBuffer cmdBuf = m_cmdBufs[m_fillBuf];

        if (m_boundTarget != target)
        {
            if (m_boundTarget)
            {
                SetImageLayout(cmdBuf, m_boundTarget->m_colorTex, VK_IMAGE_ASPECT_COLOR_BIT,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, 1);
                SetImageLayout(cmdBuf, m_boundTarget->m_depthTex, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, 1);
            }

            SetImageLayout(cmdBuf, ctarget->m_colorTex, VK_IMAGE_ASPECT_COLOR_BIT,
                           ctarget->m_layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, 1);
            SetImageLayout(cmdBuf, ctarget->m_depthTex, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                           ctarget->m_layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1, 1);
            ctarget->m_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

            m_boundTarget = ctarget;
        }

        vk::CmdBeginRenderPass(cmdBuf, &ctarget->m_passBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    }

    void setViewport(const SWindowRect& rect, float znear, float zfar)
    {
        if (m_boundTarget)
        {
            VkViewport vp = {float(rect.location[0]),
                             float(std::max(0, int(m_boundTarget->m_height) - rect.location[1] - rect.size[1])),
                             float(rect.size[0]), float(rect.size[1]), znear, zfar};
            vk::CmdSetViewport(m_cmdBufs[m_fillBuf], 0, 1, &vp);
        }
    }

    void setScissor(const SWindowRect& rect)
    {
        if (m_boundTarget)
        {
            VkRect2D vkrect =
            {
                {int32_t(rect.location[0]),
                 int32_t(std::max(0, int(m_boundTarget->m_height) - rect.location[1] - rect.size[1]))},
                {uint32_t(rect.size[0]), uint32_t(rect.size[1])}
            };
            vk::CmdSetScissor(m_cmdBufs[m_fillBuf], 0, 1, &vkrect);
        }
    }

    std::unordered_map<VulkanTextureR*, std::pair<size_t, size_t>> m_texResizes;
    void resizeRenderTexture(ITextureR* tex, size_t width, size_t height)
    {
        VulkanTextureR* ctex = static_cast<VulkanTextureR*>(tex);
        m_texResizes[ctex] = std::make_pair(width, height);
    }

    void schedulePostFrameHandler(std::function<void(void)>&& func)
    {
        func();
    }

    float m_clearColor[4] = {0.0,0.0,0.0,0.0};
    void setClearColor(const float rgba[4])
    {
        m_clearColor[0] = rgba[0];
        m_clearColor[1] = rgba[1];
        m_clearColor[2] = rgba[2];
        m_clearColor[3] = rgba[3];
    }

    void clearTarget(bool render=true, bool depth=true)
    {
        if (!m_boundTarget)
            return;
        VkClearAttachment clr[2] = {};
        VkClearRect rect = {};
        rect.layerCount = 1;
        rect.rect.extent.width = m_boundTarget->m_width;
        rect.rect.extent.height = m_boundTarget->m_height;

        if (render && depth)
        {
            clr[0].clearValue.color.float32[0] = m_clearColor[0];
            clr[0].clearValue.color.float32[1] = m_clearColor[1];
            clr[0].clearValue.color.float32[2] = m_clearColor[2];
            clr[0].clearValue.color.float32[3] = m_clearColor[3];
            clr[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            clr[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            clr[1].clearValue.depthStencil.depth = 1.f;
            vk::CmdClearAttachments(m_cmdBufs[m_fillBuf], 2, clr, 1, &rect);
        }
        else if (render)
        {
            clr[0].clearValue.color.float32[0] = m_clearColor[0];
            clr[0].clearValue.color.float32[1] = m_clearColor[1];
            clr[0].clearValue.color.float32[2] = m_clearColor[2];
            clr[0].clearValue.color.float32[3] = m_clearColor[3];
            clr[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vk::CmdClearAttachments(m_cmdBufs[m_fillBuf], 1, clr, 1, &rect);
        }
        else if (depth)
        {
            clr[0].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            clr[0].clearValue.depthStencil.depth = 1.f;
            vk::CmdClearAttachments(m_cmdBufs[m_fillBuf], 1, clr, 1, &rect);
        }
    }

    void draw(size_t start, size_t count)
    {
        vk::CmdDraw(m_cmdBufs[m_fillBuf], count, 1, start, 0);
    }

    void drawIndexed(size_t start, size_t count)
    {
        vk::CmdDrawIndexed(m_cmdBufs[m_fillBuf], count, 1, start, 0, 0);
    }

    void drawInstances(size_t start, size_t count, size_t instCount)
    {
        vk::CmdDraw(m_cmdBufs[m_fillBuf], count, instCount, start, 0);
    }

    void drawInstancesIndexed(size_t start, size_t count, size_t instCount)
    {
        vk::CmdDrawIndexed(m_cmdBufs[m_fillBuf], count, instCount, start, 0, 0);
    }

    ITextureR* m_resolveDispSource = nullptr;
    void resolveDisplay(ITextureR* source)
    {
        m_resolveDispSource = source;
    }

    bool _resolveDisplay()
    {
        if (!m_resolveDispSource)
            return false;
        VulkanContext::Window::SwapChain& sc = m_windowCtx->m_swapChains[m_windowCtx->m_activeSwapChain];
        if (!sc.m_swapChain)
            return false;

        VkCommandBuffer cmdBuf = m_cmdBufs[m_drawBuf];
        VulkanTextureR* csource = static_cast<VulkanTextureR*>(m_resolveDispSource);

        ThrowIfFailed(vk::AcquireNextImageKHR(m_ctx->m_dev, sc.m_swapChain, UINT64_MAX,
                                              m_swapChainReadySem, nullptr, &sc.m_backBuf));
        VulkanContext::Window::SwapChain::Buffer& dest = sc.m_bufs[sc.m_backBuf];
        SetImageLayout(cmdBuf, dest.m_image, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, 1);

        if (m_resolveDispSource == m_boundTarget)
            SetImageLayout(cmdBuf, csource->m_colorTex, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, 1);

        if (csource->m_samples > 1)
        {
            VkImageResolve resolveInfo = {};
            resolveInfo.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            resolveInfo.srcSubresource.mipLevel = 0;
            resolveInfo.srcSubresource.baseArrayLayer = 0;
            resolveInfo.srcSubresource.layerCount = 1;
            resolveInfo.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            resolveInfo.dstSubresource.mipLevel = 0;
            resolveInfo.dstSubresource.baseArrayLayer = 0;
            resolveInfo.dstSubresource.layerCount = 1;
            resolveInfo.extent.width = csource->m_width;
            resolveInfo.extent.height = csource->m_height;
            resolveInfo.extent.depth = 1;
            vk::CmdResolveImage(cmdBuf,
                                csource->m_colorTex, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                dest.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                1, &resolveInfo);
        }
        else
        {
            VkImageCopy copyInfo = {};
            copyInfo.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyInfo.srcSubresource.mipLevel = 0;
            copyInfo.srcSubresource.baseArrayLayer = 0;
            copyInfo.srcSubresource.layerCount = 1;
            copyInfo.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyInfo.dstSubresource.mipLevel = 0;
            copyInfo.dstSubresource.baseArrayLayer = 0;
            copyInfo.dstSubresource.layerCount = 1;
            copyInfo.extent.width = csource->m_width;
            copyInfo.extent.height = csource->m_height;
            copyInfo.extent.depth = 1;
            vk::CmdCopyImage(cmdBuf,
                             csource->m_colorTex, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             dest.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             1, &copyInfo);
        }

        SetImageLayout(cmdBuf, dest.m_image, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 1, 1);
        dest.m_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        if (m_resolveDispSource == m_boundTarget)
            SetImageLayout(cmdBuf, csource->m_colorTex, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, 1);

        m_resolveDispSource = nullptr;
        return true;
    }

    void resolveBindTexture(ITextureR* texture, const SWindowRect& rect, bool tlOrigin,
                            int bindIdx, bool color, bool depth)
    {
        VkCommandBuffer cmdBuf = m_cmdBufs[m_fillBuf];
        VulkanTextureR* ctexture = static_cast<VulkanTextureR*>(texture);

        vk::CmdEndRenderPass(cmdBuf);

        VkImageCopy copyInfo = {};
        SWindowRect intersectRect = rect.intersect(SWindowRect(0, 0, ctexture->m_width, ctexture->m_height));
        copyInfo.srcOffset.y = tlOrigin ? intersectRect.location[1] :
            (ctexture->m_height - intersectRect.size[1] - intersectRect.location[1]);
        copyInfo.srcOffset.x = intersectRect.location[0];
        copyInfo.dstOffset = copyInfo.srcOffset;
        copyInfo.extent.width = intersectRect.size[0];
        copyInfo.extent.height = intersectRect.size[1];
        copyInfo.extent.depth = 1;
        copyInfo.dstSubresource.mipLevel = 0;
        copyInfo.dstSubresource.baseArrayLayer = 0;
        copyInfo.dstSubresource.layerCount = 1;
        copyInfo.srcSubresource.mipLevel = 0;
        copyInfo.srcSubresource.baseArrayLayer = 0;
        copyInfo.srcSubresource.layerCount = 1;

        if (color && ctexture->m_colorBindCount)
        {
            if (ctexture == m_boundTarget)
                SetImageLayout(cmdBuf, ctexture->m_colorTex, VK_IMAGE_ASPECT_COLOR_BIT,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, 1);

            SetImageLayout(cmdBuf, ctexture->m_colorBindTex[bindIdx], VK_IMAGE_ASPECT_COLOR_BIT,
                           ctexture->m_colorBindLayout[bindIdx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, 1);

            copyInfo.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyInfo.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

            vk::CmdCopyImage(cmdBuf,
                             ctexture->m_colorTex, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             ctexture->m_colorBindTex[bindIdx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             1, &copyInfo);

            if (ctexture == m_boundTarget)
                SetImageLayout(cmdBuf, ctexture->m_colorTex, VK_IMAGE_ASPECT_COLOR_BIT,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, 1);

            SetImageLayout(cmdBuf, ctexture->m_colorBindTex[bindIdx], VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, 1);
            ctexture->m_colorBindLayout[bindIdx] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        if (depth && ctexture->m_depthBindCount)
        {
            if (ctexture == m_boundTarget)
                SetImageLayout(cmdBuf, ctexture->m_depthTex, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, 1);

            SetImageLayout(cmdBuf, ctexture->m_depthBindTex[bindIdx], VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                           ctexture->m_depthBindLayout[bindIdx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, 1);

            copyInfo.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            copyInfo.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

            vk::CmdCopyImage(cmdBuf,
                             ctexture->m_depthTex, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             ctexture->m_depthBindTex[bindIdx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             1, &copyInfo);

            if (ctexture == m_boundTarget)
                SetImageLayout(cmdBuf, ctexture->m_depthTex, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1, 1);

            SetImageLayout(cmdBuf, ctexture->m_depthBindTex[bindIdx], VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, 1);
            ctexture->m_depthBindLayout[bindIdx] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        vk::CmdBeginRenderPass(cmdBuf, &m_boundTarget->m_passBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    }

    void execute();
};

VulkanGraphicsBufferD::~VulkanGraphicsBufferD()
{
    vk::DestroyBuffer(m_q->m_ctx->m_dev, m_bufferInfo[0].buffer, nullptr);
    vk::DestroyBuffer(m_q->m_ctx->m_dev, m_bufferInfo[1].buffer, nullptr);
}

VulkanTextureD::~VulkanTextureD()
{
    vk::DestroyImageView(m_q->m_ctx->m_dev, m_gpuView[0], nullptr);
    vk::DestroyImageView(m_q->m_ctx->m_dev, m_gpuView[1], nullptr);
    vk::DestroyBuffer(m_q->m_ctx->m_dev, m_cpuBuf[0], nullptr);
    vk::DestroyBuffer(m_q->m_ctx->m_dev, m_cpuBuf[1], nullptr);
    vk::DestroyImage(m_q->m_ctx->m_dev, m_gpuTex[0], nullptr);
    vk::DestroyImage(m_q->m_ctx->m_dev, m_gpuTex[1], nullptr);
    vk::FreeMemory(m_q->m_ctx->m_dev, m_cpuMem, nullptr);
}

void VulkanTextureR::doDestroy()
{
    if (m_framebuffer)
    {
        vk::DestroyFramebuffer(m_q->m_ctx->m_dev, m_framebuffer, nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }
    if (m_colorView)
    {
        vk::DestroyImageView(m_q->m_ctx->m_dev, m_colorView, nullptr);
        m_colorView = VK_NULL_HANDLE;
    }
    if (m_colorTex)
    {
        vk::DestroyImage(m_q->m_ctx->m_dev, m_colorTex, nullptr);
        m_colorTex = VK_NULL_HANDLE;
    }
    if (m_depthView)
    {
        vk::DestroyImageView(m_q->m_ctx->m_dev, m_depthView, nullptr);
        m_depthView = VK_NULL_HANDLE;
    }
    if (m_depthTex)
    {
        vk::DestroyImage(m_q->m_ctx->m_dev, m_depthTex, nullptr);
        m_depthTex = VK_NULL_HANDLE;
    }
    for (size_t i=0 ; i<MAX_BIND_TEXS ; ++i)
        if (m_colorBindView[i])
        {
            vk::DestroyImageView(m_q->m_ctx->m_dev, m_colorBindView[i], nullptr);
            m_colorBindView[i] = VK_NULL_HANDLE;
        }
    for (size_t i=0 ; i<MAX_BIND_TEXS ; ++i)
        if (m_colorBindTex[i])
        {
            vk::DestroyImage(m_q->m_ctx->m_dev, m_colorBindTex[i], nullptr);
            m_colorBindTex[i] = VK_NULL_HANDLE;
        }
    for (size_t i=0 ; i<MAX_BIND_TEXS ; ++i)
        if (m_depthBindView[i])
        {
            vk::DestroyImageView(m_q->m_ctx->m_dev, m_depthBindView[i], nullptr);
            m_depthBindView[i] = VK_NULL_HANDLE;
        }
    for (size_t i=0 ; i<MAX_BIND_TEXS ; ++i)
        if (m_depthBindTex[i])
        {
            vk::DestroyImage(m_q->m_ctx->m_dev, m_depthBindTex[i], nullptr);
            m_depthBindTex[i] = VK_NULL_HANDLE;
        }
    if (m_gpuMem)
    {
        vk::FreeMemory(m_q->m_ctx->m_dev, m_gpuMem, nullptr);
        m_gpuMem = VK_NULL_HANDLE;
    }
}

VulkanTextureR::~VulkanTextureR()
{
    vk::DestroyFramebuffer(m_q->m_ctx->m_dev, m_framebuffer, nullptr);
    vk::DestroyImageView(m_q->m_ctx->m_dev, m_colorView, nullptr);
    vk::DestroyImage(m_q->m_ctx->m_dev, m_colorTex, nullptr);
    vk::DestroyImageView(m_q->m_ctx->m_dev, m_depthView, nullptr);
    vk::DestroyImage(m_q->m_ctx->m_dev, m_depthTex, nullptr);
    for (size_t i=0 ; i<MAX_BIND_TEXS ; ++i)
        if (m_colorBindView[i])
            vk::DestroyImageView(m_q->m_ctx->m_dev, m_colorBindView[i], nullptr);
    for (size_t i=0 ; i<MAX_BIND_TEXS ; ++i)
        if (m_colorBindTex[i])
            vk::DestroyImage(m_q->m_ctx->m_dev, m_colorBindTex[i], nullptr);
    for (size_t i=0 ; i<MAX_BIND_TEXS ; ++i)
        if (m_depthBindView[i])
            vk::DestroyImageView(m_q->m_ctx->m_dev, m_depthBindView[i], nullptr);
    for (size_t i=0 ; i<MAX_BIND_TEXS ; ++i)
        if (m_depthBindTex[i])
            vk::DestroyImage(m_q->m_ctx->m_dev, m_depthBindTex[i], nullptr);
    vk::FreeMemory(m_q->m_ctx->m_dev, m_gpuMem, nullptr);
    if (m_q->m_boundTarget == this)
        m_q->m_boundTarget = nullptr;
}

void VulkanGraphicsBufferD::update(int b)
{
    int slot = 1 << b;
    if ((slot & m_validSlots) == 0)
    {
        void* ptr;
        ThrowIfFailed(vk::MapMemory(m_q->m_ctx->m_dev, m_mem,
                                    m_memOffset[b], m_cpuSz, 0, &ptr));
        memmove(ptr, m_cpuBuf.get(), m_cpuSz);
        vk::UnmapMemory(m_q->m_ctx->m_dev, m_mem);
        m_validSlots |= slot;
    }
}

void VulkanGraphicsBufferD::load(const void* data, size_t sz)
{
    size_t bufSz = std::min(sz, m_cpuSz);
    memmove(m_cpuBuf.get(), data, bufSz);
    m_validSlots = 0;
}
void* VulkanGraphicsBufferD::map(size_t sz)
{
    if (sz > m_cpuSz)
        return nullptr;
    return m_cpuBuf.get();
}
void VulkanGraphicsBufferD::unmap()
{
    m_validSlots = 0;
}

void VulkanTextureD::update(int b)
{
    int slot = 1 << b;
    if ((slot & m_validSlots) == 0)
    {
        m_q->stallDynamicUpload();
        VkCommandBuffer cmdBuf = m_q->m_dynamicCmdBufs[b];

        /* map memory and copy staging data */
        uint8_t* mappedData;
        ThrowIfFailed(vk::MapMemory(m_q->m_ctx->m_dev, m_cpuMem, m_cpuOffsets[b], m_cpuSz, 0, reinterpret_cast<void**>(&mappedData)));
        memmove(mappedData, m_stagingBuf.get(), m_cpuSz);
        vk::UnmapMemory(m_q->m_ctx->m_dev, m_cpuMem);

        SetImageLayout(cmdBuf, m_gpuTex[b], VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, 1);

        /* Put the copy command into the command buffer */
        VkBufferImageCopy copyRegion = {};
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent.width = m_width;
        copyRegion.imageExtent.height = m_height;
        copyRegion.imageExtent.depth = 1;
        copyRegion.bufferOffset = 0;

        vk::CmdCopyBufferToImage(cmdBuf,
                                 m_cpuBuf[b],
                                 m_gpuTex[b],
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 1,
                                 &copyRegion);

        /* Set the layout for the texture image from DESTINATION_OPTIMAL to
         * SHADER_READ_ONLY */
        SetImageLayout(cmdBuf, m_gpuTex[b], VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, 1);

        m_validSlots |= slot;
    }
}
void VulkanTextureD::load(const void* data, size_t sz)
{
    size_t bufSz = std::min(sz, m_cpuSz);
    memmove(m_stagingBuf.get(), data, bufSz);
    m_validSlots = 0;
}
void* VulkanTextureD::map(size_t sz)
{
    if (sz > m_cpuSz)
        return nullptr;
    return m_stagingBuf.get();
}
void VulkanTextureD::unmap()
{
    m_validSlots = 0;
}

void VulkanDataFactoryImpl::destroyData(IGraphicsData* d)
{
    VulkanData* data = static_cast<VulkanData*>(d);
    data->m_dead = true;
}

void VulkanDataFactoryImpl::destroyPool(IGraphicsBufferPool* p)
{
    VulkanPool* pool = static_cast<VulkanPool*>(p);
    pool->m_dead = true;
}

void VulkanDataFactoryImpl::destroyAllData()
{
    std::unique_lock<std::mutex> lk(m_committedMutex);
    for (VulkanData* data : m_committedData)
        data->decrement();
    for (IGraphicsBufferPool* pool : m_committedPools)
        delete static_cast<VulkanPool*>(pool);
    m_committedData.clear();
    m_committedPools.clear();
}

VulkanDataFactoryImpl::VulkanDataFactoryImpl(IGraphicsContext* parent,
                                             VulkanContext* ctx, uint32_t drawSamples)
: m_parent(parent), m_ctx(ctx), m_drawSamples(drawSamples)
{
    VkDescriptorSetLayoutBinding layoutBindings[BOO_GLSL_MAX_UNIFORM_COUNT + BOO_GLSL_MAX_TEXTURE_COUNT];
    for (int i=0 ; i<BOO_GLSL_MAX_UNIFORM_COUNT ; ++i)
    {
        layoutBindings[i].binding = i;
        layoutBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        layoutBindings[i].descriptorCount = 1;
        layoutBindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        layoutBindings[i].pImmutableSamplers = nullptr;
    }
    for (int i=BOO_GLSL_MAX_UNIFORM_COUNT ; i<BOO_GLSL_MAX_UNIFORM_COUNT+BOO_GLSL_MAX_TEXTURE_COUNT ; ++i)
    {
        layoutBindings[i].binding = i;
        layoutBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        layoutBindings[i].descriptorCount = 1;
        layoutBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        layoutBindings[i].pImmutableSamplers = &ctx->m_linearSampler;
    }

    VkDescriptorSetLayoutCreateInfo descriptorLayout = {};
    descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayout.pNext = nullptr;
    descriptorLayout.bindingCount = BOO_GLSL_MAX_UNIFORM_COUNT + BOO_GLSL_MAX_TEXTURE_COUNT;
    descriptorLayout.pBindings = layoutBindings;

    ThrowIfFailed(vk::CreateDescriptorSetLayout(ctx->m_dev, &descriptorLayout, nullptr,
                                                &ctx->m_descSetLayout));

    VkPipelineLayoutCreateInfo pipelineLayout = {};
    pipelineLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayout.setLayoutCount = 1;
    pipelineLayout.pSetLayouts = &ctx->m_descSetLayout;
    ThrowIfFailed(vk::CreatePipelineLayout(ctx->m_dev, &pipelineLayout, nullptr, &ctx->m_pipelinelayout));

    VkAttachmentDescription attachments[2] = {};

    /* color attachment */
    attachments[0].format = ctx->m_displayFormat;
    attachments[0].samples = VkSampleCountFlagBits(drawSamples);
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorAttachmentRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    /* depth attachment */
    attachments[1].format = VK_FORMAT_D24_UNORM_S8_UINT;
    attachments[1].samples = VkSampleCountFlagBits(drawSamples);
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthAttachmentRef = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    /* render subpass */
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    /* render pass */
    VkRenderPassCreateInfo renderPass = {};
    renderPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPass.attachmentCount = 2;
    renderPass.pAttachments = attachments;
    renderPass.subpassCount = 1;
    renderPass.pSubpasses = &subpass;
    ThrowIfFailed(vk::CreateRenderPass(ctx->m_dev, &renderPass, nullptr, &ctx->m_pass));
}

static uint64_t CompileVert(std::vector<unsigned int>& out, const char* vertSource, uint64_t srcKey,
                            VulkanDataFactoryImpl& factory)
{
    const EShMessages messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
    glslang::TShader vs(EShLangVertex);
    vs.setStrings(&vertSource, 1);
    if (!vs.parse(&glslang::DefaultTBuiltInResource, 110, false, messages))
    {
        printf("%s\n", vertSource);
        Log.report(logvisor::Fatal, "unable to compile vertex shader\n%s", vs.getInfoLog());
    }

    glslang::TProgram prog;
    prog.addShader(&vs);
    if (!prog.link(messages))
    {
        Log.report(logvisor::Fatal, "unable to link shader program\n%s", prog.getInfoLog());
    }
    glslang::GlslangToSpv(*prog.getIntermediate(EShLangVertex), out);
    //spv::Disassemble(std::cerr, out);

    XXH64_state_t hashState;
    XXH64_reset(&hashState, 0);
    XXH64_update(&hashState, out.data(), out.size() * sizeof(unsigned int));
    uint64_t binKey = XXH64_digest(&hashState);
    factory.m_sourceToBinary[srcKey] = binKey;
    return binKey;
}

static uint64_t CompileFrag(std::vector<unsigned int>& out, const char* fragSource, uint64_t srcKey,
                            VulkanDataFactoryImpl& factory)
{
    const EShMessages messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
    glslang::TShader fs(EShLangFragment);
    fs.setStrings(&fragSource, 1);
    if (!fs.parse(&glslang::DefaultTBuiltInResource, 110, false, messages))
    {
        printf("%s\n", fragSource);
        Log.report(logvisor::Fatal, "unable to compile fragment shader\n%s", fs.getInfoLog());
    }

    glslang::TProgram prog;
    prog.addShader(&fs);
    if (!prog.link(messages))
    {
        Log.report(logvisor::Fatal, "unable to link shader program\n%s", prog.getInfoLog());
    }
    glslang::GlslangToSpv(*prog.getIntermediate(EShLangFragment), out);
    //spv::Disassemble(std::cerr, out);

    XXH64_state_t hashState;
    XXH64_reset(&hashState, 0);
    XXH64_update(&hashState, out.data(), out.size() * sizeof(unsigned int));
    uint64_t binKey = XXH64_digest(&hashState);
    factory.m_sourceToBinary[srcKey] = binKey;
    return binKey;
}

IShaderPipeline* VulkanDataFactory::Context::newShaderPipeline
(const char* vertSource, const char* fragSource,
 std::vector<unsigned int>* vertBlobOut, std::vector<unsigned int>* fragBlobOut,
 std::vector<unsigned char>* pipelineBlob, IVertexFormat* vtxFmt,
 BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
 ZTest depthTest, bool depthWrite, bool colorWrite,
 bool alphaWrite, CullMode culling)
{
    VulkanData* d = static_cast<VulkanData*>(VulkanDataFactoryImpl::m_deferredData.get());
    VulkanDataFactoryImpl& factory = static_cast<VulkanDataFactoryImpl&>(m_parent);

    XXH64_state_t hashState;
    uint64_t srcHashes[2] = {};
    uint64_t binHashes[2] = {};
    XXH64_reset(&hashState, 0);
    if (vertSource)
    {
        XXH64_update(&hashState, vertSource, strlen(vertSource));
        srcHashes[0] = XXH64_digest(&hashState);
        auto binSearch = factory.m_sourceToBinary.find(srcHashes[0]);
        if (binSearch != factory.m_sourceToBinary.cend())
            binHashes[0] = binSearch->second;
    }
    else if (vertBlobOut && vertBlobOut->size())
    {
        XXH64_update(&hashState, vertBlobOut->data(), vertBlobOut->size() * sizeof(unsigned int));
        binHashes[0] = XXH64_digest(&hashState);
    }
    XXH64_reset(&hashState, 0);
    if (fragSource)
    {
        XXH64_update(&hashState, fragSource, strlen(fragSource));
        srcHashes[1] = XXH64_digest(&hashState);
        auto binSearch = factory.m_sourceToBinary.find(srcHashes[1]);
        if (binSearch != factory.m_sourceToBinary.cend())
            binHashes[1] = binSearch->second;
    }
    else if (fragBlobOut && fragBlobOut->size())
    {
        XXH64_update(&hashState, fragBlobOut->data(), fragBlobOut->size() * sizeof(unsigned int));
        binHashes[1] = XXH64_digest(&hashState);
    }

    if (vertBlobOut && vertBlobOut->empty())
        binHashes[0] = CompileVert(*vertBlobOut, vertSource, srcHashes[0], factory);

    if (fragBlobOut && fragBlobOut->empty())
        binHashes[1] = CompileFrag(*fragBlobOut, fragSource, srcHashes[1], factory);

    VkShaderModuleCreateInfo smCreateInfo = {};
    smCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smCreateInfo.pNext = nullptr;
    smCreateInfo.flags = 0;

    VulkanShareableShader::Token vertShader;
    VulkanShareableShader::Token fragShader;
    auto vertFind = binHashes[0] ? factory.m_sharedShaders.find(binHashes[0]) :
                                   factory.m_sharedShaders.end();
    if (vertFind != factory.m_sharedShaders.end())
    {
        vertShader = vertFind->second->lock();
    }
    else
    {
        std::vector<unsigned int> vertBlob;
        const std::vector<unsigned int>* useVertBlob;
        if (vertBlobOut)
        {
            useVertBlob = vertBlobOut;
        }
        else
        {
            useVertBlob = &vertBlob;
            binHashes[0] = CompileVert(vertBlob, vertSource, srcHashes[0], factory);
        }

        smCreateInfo.codeSize = useVertBlob->size() * sizeof(unsigned int);
        smCreateInfo.pCode = useVertBlob->data();
        VkShaderModule vertModule;
        ThrowIfFailed(vk::CreateShaderModule(factory.m_ctx->m_dev, &smCreateInfo, nullptr, &vertModule));

        auto it =
        factory.m_sharedShaders.emplace(std::make_pair(binHashes[0],
            std::make_unique<VulkanShareableShader>(factory, srcHashes[0], binHashes[0],
                                                    factory.m_ctx->m_dev, vertModule))).first;
        vertShader = it->second->lock();
    }
    auto fragFind = binHashes[1] ? factory.m_sharedShaders.find(binHashes[1]) :
                                   factory.m_sharedShaders.end();
    if (fragFind != factory.m_sharedShaders.end())
    {
        fragShader = fragFind->second->lock();
    }
    else
    {
        std::vector<unsigned int> fragBlob;
        const std::vector<unsigned int>* useFragBlob;
        if (fragBlobOut)
        {
            useFragBlob = fragBlobOut;
        }
        else
        {
            useFragBlob = &fragBlob;
            binHashes[1] = CompileFrag(fragBlob, fragSource, srcHashes[1], factory);
        }

        smCreateInfo.codeSize = useFragBlob->size() * sizeof(unsigned int);
        smCreateInfo.pCode = useFragBlob->data();
        VkShaderModule fragModule;
        ThrowIfFailed(vk::CreateShaderModule(factory.m_ctx->m_dev, &smCreateInfo, nullptr, &fragModule));

        auto it =
        factory.m_sharedShaders.emplace(std::make_pair(binHashes[1],
            std::make_unique<VulkanShareableShader>(factory, srcHashes[1], binHashes[1],
                                                    factory.m_ctx->m_dev, fragModule))).first;
        fragShader = it->second->lock();
    }


    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    if (pipelineBlob)
    {
        VkPipelineCacheCreateInfo cacheDataInfo = {};
        cacheDataInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        cacheDataInfo.pNext = nullptr;

        cacheDataInfo.initialDataSize = pipelineBlob->size();
        if (cacheDataInfo.initialDataSize)
            cacheDataInfo.pInitialData = pipelineBlob->data();

        ThrowIfFailed(vk::CreatePipelineCache(factory.m_ctx->m_dev, &cacheDataInfo, nullptr, &pipelineCache));
    }

    VulkanShaderPipeline* retval = new VulkanShaderPipeline(d, factory.m_ctx, std::move(vertShader), std::move(fragShader),
                                                            pipelineCache, static_cast<const VulkanVertexFormat*>(vtxFmt),
                                                            srcFac, dstFac, prim, depthTest, depthWrite, colorWrite,
                                                            alphaWrite, culling);

    if (pipelineBlob && pipelineBlob->empty())
    {
        size_t cacheSz = 0;
        ThrowIfFailed(vk::GetPipelineCacheData(factory.m_ctx->m_dev, pipelineCache, &cacheSz, nullptr));
        if (cacheSz)
        {
            pipelineBlob->resize(cacheSz);
            ThrowIfFailed(vk::GetPipelineCacheData(factory.m_ctx->m_dev, pipelineCache, &cacheSz, pipelineBlob->data()));
            pipelineBlob->resize(cacheSz);
        }
    }

    d->m_SPs.emplace_back(retval);
    return retval;
}

IGraphicsBufferS* VulkanDataFactory::Context::newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count)
{
    VulkanData* d = static_cast<VulkanData*>(VulkanDataFactoryImpl::m_deferredData.get());
    VulkanDataFactoryImpl& factory = static_cast<VulkanDataFactoryImpl&>(m_parent);
    VulkanGraphicsBufferS* retval = new VulkanGraphicsBufferS(d, use, factory.m_ctx, data, stride, count);
    d->m_SBufs.emplace_back(retval);
    return retval;
}

IGraphicsBufferD* VulkanDataFactory::Context::newDynamicBuffer(BufferUse use, size_t stride, size_t count)
{
    VulkanData* d = static_cast<VulkanData*>(VulkanDataFactoryImpl::m_deferredData.get());
    VulkanDataFactoryImpl& factory = static_cast<VulkanDataFactoryImpl&>(m_parent);
    VulkanCommandQueue* q = static_cast<VulkanCommandQueue*>(factory.m_parent->getCommandQueue());
    VulkanGraphicsBufferD* retval = new VulkanGraphicsBufferD(d, q, use, factory.m_ctx, stride, count);
    d->m_DBufs.emplace_back(retval);
    return retval;
}

ITextureS* VulkanDataFactory::Context::newStaticTexture(size_t width, size_t height, size_t mips,
                                                        TextureFormat fmt, const void* data, size_t sz)
{
    VulkanData* d = static_cast<VulkanData*>(VulkanDataFactoryImpl::m_deferredData.get());
    VulkanDataFactoryImpl& factory = static_cast<VulkanDataFactoryImpl&>(m_parent);
    VulkanTextureS* retval = new VulkanTextureS(d, factory.m_ctx, width, height, mips, fmt, data, sz);
    d->m_STexs.emplace_back(retval);
    return retval;
}

ITextureSA* VulkanDataFactory::Context::newStaticArrayTexture(size_t width, size_t height, size_t layers, size_t mips,
                                                              TextureFormat fmt, const void* data, size_t sz)
{
    VulkanData* d = static_cast<VulkanData*>(VulkanDataFactoryImpl::m_deferredData.get());
    VulkanDataFactoryImpl& factory = static_cast<VulkanDataFactoryImpl&>(m_parent);
    VulkanTextureSA* retval = new VulkanTextureSA(d, factory.m_ctx, width, height, layers, mips, fmt, data, sz);
    d->m_SATexs.emplace_back(retval);
    return retval;
}

ITextureD* VulkanDataFactory::Context::newDynamicTexture(size_t width, size_t height, TextureFormat fmt)
{
    VulkanData* d = static_cast<VulkanData*>(VulkanDataFactoryImpl::m_deferredData.get());
    VulkanDataFactoryImpl& factory = static_cast<VulkanDataFactoryImpl&>(m_parent);
    VulkanCommandQueue* q = static_cast<VulkanCommandQueue*>(factory.m_parent->getCommandQueue());
    VulkanTextureD* retval = new VulkanTextureD(d, q, factory.m_ctx, width, height, fmt);
    d->m_DTexs.emplace_back(retval);
    return retval;
}

ITextureR* VulkanDataFactory::Context::newRenderTexture(size_t width, size_t height,
                                                        size_t colorBindCount, size_t depthBindCount)
{
    VulkanData* d = static_cast<VulkanData*>(VulkanDataFactoryImpl::m_deferredData.get());
    VulkanDataFactoryImpl& factory = static_cast<VulkanDataFactoryImpl&>(m_parent);
    VulkanCommandQueue* q = static_cast<VulkanCommandQueue*>(factory.m_parent->getCommandQueue());
    VulkanTextureR* retval = new VulkanTextureR(d, factory.m_ctx, q, width, height, factory.m_drawSamples,
                                                colorBindCount, depthBindCount);
    d->m_RTexs.emplace_back(retval);
    return retval;
}

IVertexFormat* VulkanDataFactory::Context::newVertexFormat(size_t elementCount,
                                                           const VertexElementDescriptor* elements,
                                                           size_t baseVert, size_t baseInst)
{
    VulkanData* d = static_cast<VulkanData*>(VulkanDataFactoryImpl::m_deferredData.get());
    VulkanVertexFormat* retval = new struct VulkanVertexFormat(d, elementCount, elements);
    d->m_VFmts.emplace_back(retval);
    return retval;
}

IShaderDataBinding* VulkanDataFactory::Context::newShaderDataBinding(IShaderPipeline* pipeline,
        IVertexFormat* /*vtxFormat*/,
        IGraphicsBuffer* vbuf, IGraphicsBuffer* instVbuf, IGraphicsBuffer* ibuf,
        size_t ubufCount, IGraphicsBuffer** ubufs, const PipelineStage* /*ubufStages*/,
        const size_t* ubufOffs, const size_t* ubufSizes,
        size_t texCount, ITexture** texs,
        const int* bindIdxs, const bool* bindDepth,
        size_t baseVert, size_t baseInst)
{
    VulkanDataFactoryImpl& factory = static_cast<VulkanDataFactoryImpl&>(m_parent);
    VulkanData* d = static_cast<VulkanData*>(VulkanDataFactoryImpl::m_deferredData.get());
    VulkanShaderDataBinding* retval =
        new VulkanShaderDataBinding(d, factory.m_ctx, pipeline, vbuf, instVbuf, ibuf,
                                    ubufCount, ubufs, ubufOffs, ubufSizes, texCount, texs,
                                    bindIdxs, bindDepth, baseVert, baseInst);
    d->m_SBinds.emplace_back(retval);
    return retval;
}

GraphicsDataToken VulkanDataFactoryImpl::commitTransaction
    (const std::function<bool(IGraphicsDataFactory::Context&)>& trans)
{
    if (m_deferredData.get())
        Log.report(logvisor::Fatal, "nested commitTransaction usage detected");
    m_deferredData.reset(new VulkanData(m_ctx));

    Context ctx(*this);
    if (!trans(ctx))
    {
        delete m_deferredData.get();
        m_deferredData.reset();
        return GraphicsDataToken(this, nullptr);
    }

    VulkanData* retval = static_cast<VulkanData*>(m_deferredData.get());

    /* size up resources */
    uint32_t bufMemTypeBits = ~0;
    VkDeviceSize bufMemSize = 0;
    uint32_t texMemTypeBits = ~0;
    VkDeviceSize texMemSize = 0;

    for (std::unique_ptr<VulkanGraphicsBufferS>& buf : retval->m_SBufs)
        bufMemSize = buf->sizeForGPU(m_ctx, bufMemTypeBits, bufMemSize);

    for (std::unique_ptr<VulkanGraphicsBufferD>& buf : retval->m_DBufs)
        bufMemSize = buf->sizeForGPU(m_ctx, bufMemTypeBits, bufMemSize);

    for (std::unique_ptr<VulkanTextureS>& tex : retval->m_STexs)
        texMemSize = tex->sizeForGPU(m_ctx, texMemTypeBits, texMemSize);

    for (std::unique_ptr<VulkanTextureSA>& tex : retval->m_SATexs)
        texMemSize = tex->sizeForGPU(m_ctx, texMemTypeBits, texMemSize);

    for (std::unique_ptr<VulkanTextureD>& tex : retval->m_DTexs)
        texMemSize = tex->sizeForGPU(m_ctx, texMemTypeBits, texMemSize);

    /* allocate memory and place textures */
    if (bufMemSize)
    {
        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAlloc.allocationSize = bufMemSize;
        ThrowIfFalse(MemoryTypeFromProperties(m_ctx, bufMemTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                              &memAlloc.memoryTypeIndex));
        ThrowIfFailed(vk::AllocateMemory(m_ctx->m_dev, &memAlloc, nullptr, &retval->m_bufMem));

        /* place resources */
        uint8_t* mappedData;
        ThrowIfFailed(vk::MapMemory(m_ctx->m_dev, retval->m_bufMem, 0, bufMemSize, 0, reinterpret_cast<void**>(&mappedData)));

        for (std::unique_ptr<VulkanGraphicsBufferS>& buf : retval->m_SBufs)
            buf->placeForGPU(m_ctx, retval->m_bufMem, mappedData);

        vk::UnmapMemory(m_ctx->m_dev, retval->m_bufMem);

        for (std::unique_ptr<VulkanGraphicsBufferD>& buf : retval->m_DBufs)
            buf->placeForGPU(m_ctx, retval->m_bufMem);
    }

    /* allocate memory and place textures */
    if (texMemSize)
    {
        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAlloc.allocationSize = texMemSize;
        ThrowIfFalse(MemoryTypeFromProperties(m_ctx, texMemTypeBits, 0, &memAlloc.memoryTypeIndex));
        ThrowIfFailed(vk::AllocateMemory(m_ctx->m_dev, &memAlloc, nullptr, &retval->m_texMem));

        for (std::unique_ptr<VulkanTextureS>& tex : retval->m_STexs)
            tex->placeForGPU(m_ctx, retval->m_texMem);

        for (std::unique_ptr<VulkanTextureSA>& tex : retval->m_SATexs)
            tex->placeForGPU(m_ctx, retval->m_texMem);

        for (std::unique_ptr<VulkanTextureD>& tex : retval->m_DTexs)
            tex->placeForGPU(m_ctx, retval->m_texMem);
    }

    /* Execute static uploads */
    ThrowIfFailed(vk::EndCommandBuffer(m_ctx->m_loadCmdBuf));
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_ctx->m_loadCmdBuf;

    /* Take exclusive lock here and submit queue */
    std::unique_lock<std::mutex> qlk(m_ctx->m_queueLock);
    ThrowIfFailed(vk::QueueWaitIdle(m_ctx->m_queue));
    ThrowIfFailed(vk::QueueSubmit(m_ctx->m_queue, 1, &submitInfo, VK_NULL_HANDLE));

    /* Commit data bindings (create descriptor sets) */
    for (std::unique_ptr<VulkanShaderDataBinding>& bind : retval->m_SBinds)
        bind->commit(m_ctx);

    /* Wait for uploads to complete */
    ThrowIfFailed(vk::QueueWaitIdle(m_ctx->m_queue));
    qlk.unlock();

    /* Reset command buffer */
    ThrowIfFailed(vk::ResetCommandBuffer(m_ctx->m_loadCmdBuf, 0));
    VkCommandBufferBeginInfo cmdBufBeginInfo = {};
    cmdBufBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufBeginInfo.flags = 0;
    ThrowIfFailed(vk::BeginCommandBuffer(m_ctx->m_loadCmdBuf, &cmdBufBeginInfo));

    /* Delete upload objects */
    for (std::unique_ptr<VulkanTextureS>& tex : retval->m_STexs)
        tex->deleteUploadObjects();

    for (std::unique_ptr<VulkanTextureSA>& tex : retval->m_SATexs)
        tex->deleteUploadObjects();

    /* All set! */
    m_deferredData.reset();
    std::unique_lock<std::mutex> lk(m_committedMutex);
    m_committedData.insert(retval);
    return GraphicsDataToken(this, retval);
}

IGraphicsBufferD* VulkanDataFactoryImpl::newPoolBuffer(IGraphicsBufferPool* p, BufferUse use,
                                                       size_t stride, size_t count)
{
    VulkanCommandQueue* q = static_cast<VulkanCommandQueue*>(m_parent->getCommandQueue());
    VulkanPool* pool = static_cast<VulkanPool*>(p);
    VulkanPoolItem* item = new VulkanPoolItem(m_ctx);
    VulkanGraphicsBufferD* retval = new VulkanGraphicsBufferD(item, q, use, m_ctx, stride, count);
    item->m_buf.reset(retval);

    /* size up resources */
    uint32_t bufMemTypeBits = ~0;
    VkDeviceSize bufMemSize = retval->sizeForGPU(m_ctx, bufMemTypeBits, 0);

    /* allocate memory */
    if (bufMemSize)
    {
        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAlloc.allocationSize = bufMemSize;
        ThrowIfFalse(MemoryTypeFromProperties(m_ctx, bufMemTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                              &memAlloc.memoryTypeIndex));
        ThrowIfFailed(vk::AllocateMemory(m_ctx->m_dev, &memAlloc, nullptr, &item->m_bufMem));

        /* place resources */
        retval->placeForGPU(m_ctx, item->m_bufMem);
    }

    pool->m_items.emplace(item);
    return retval;
}

void VulkanDataFactoryImpl::deletePoolBuffer(IGraphicsBufferPool* p, IGraphicsBufferD* buf)
{
    VulkanPool* pool = static_cast<VulkanPool*>(p);
    auto search = pool->m_items.find(static_cast<VulkanPoolItem*>(buf->m_parentData));
    if (search != pool->m_items.end())
        (*search)->m_dead = true;
}

GraphicsBufferPoolToken VulkanDataFactoryImpl::newBufferPool()
{
    std::unique_lock<std::mutex> lk(m_committedMutex);
    VulkanPool* retval = new VulkanPool;
    m_committedPools.insert(retval);
    return GraphicsBufferPoolToken(this, retval);
}

ThreadLocalPtr<struct VulkanData> VulkanDataFactoryImpl::m_deferredData;

void VulkanCommandQueue::execute()
{
    if (!m_running)
        return;

    /* Stage dynamic uploads */
    VulkanDataFactoryImpl* gfxF = static_cast<VulkanDataFactoryImpl*>(m_parent->getDataFactory());
    std::unique_lock<std::mutex> datalk(gfxF->m_committedMutex);
    for (VulkanData* d : gfxF->m_committedData)
    {
        for (std::unique_ptr<VulkanGraphicsBufferD>& b : d->m_DBufs)
            b->update(m_fillBuf);
        for (std::unique_ptr<VulkanTextureD>& t : d->m_DTexs)
            t->update(m_fillBuf);
    }
    for (VulkanPool* p : gfxF->m_committedPools)
    {
        for (auto& b : p->m_items)
            b->m_buf->update(m_fillBuf);
    }
    datalk.unlock();

    /* Perform dynamic uploads */
    std::unique_lock<std::mutex> lk(m_ctx->m_queueLock);
    if (!m_dynamicNeedsReset)
    {
        vk::EndCommandBuffer(m_dynamicCmdBufs[m_fillBuf]);

        vk::WaitForFences(m_ctx->m_dev, 1, &m_dynamicBufFence, VK_FALSE, -1);
        vk::ResetFences(m_ctx->m_dev, 1, &m_dynamicBufFence);

        VkSubmitInfo submitInfo = {};
        submitInfo.pNext = nullptr;
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 0;
        submitInfo.pWaitSemaphores = nullptr;
        submitInfo.pWaitDstStageMask = nullptr;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_dynamicCmdBufs[m_fillBuf];
        submitInfo.signalSemaphoreCount = 0;
        submitInfo.pSignalSemaphores = nullptr;
        ThrowIfFailed(vk::QueueSubmit(m_ctx->m_queue, 1, &submitInfo, m_dynamicBufFence));
    }

    vk::CmdEndRenderPass(m_cmdBufs[m_fillBuf]);

    /* Check on fence */
    if (m_submitted && vk::GetFenceStatus(m_ctx->m_dev, m_drawCompleteFence) == VK_NOT_READY)
    {
        /* Abandon this list (renderer too slow) */
        resetCommandBuffer();
        m_dynamicNeedsReset = true;
        m_resolveDispSource = nullptr;
        return;
    }
    m_submitted = false;

    vk::ResetFences(m_ctx->m_dev, 1, &m_drawCompleteFence);

    /* Clear dead data */
    datalk.lock();
    for (auto it = gfxF->m_committedData.begin() ; it != gfxF->m_committedData.end() ;)
    {
        if ((*it)->m_dead)
        {
            (*it)->decrement();
            it = gfxF->m_committedData.erase(it);
            continue;
        }
        ++it;
    }
    for (auto it = gfxF->m_committedPools.begin() ; it != gfxF->m_committedPools.end() ;)
    {
        if ((*it)->m_dead)
        {
            VulkanPool* p = *it;
            it = gfxF->m_committedPools.erase(it);
            delete p;
            continue;
        }
        else
        {
            (*it)->clearDeadBuffers();
        }
        ++it;
    }
    datalk.unlock();

    /* Perform texture and swap-chain resizes */
    if (m_ctx->_resizeSwapChains() || m_texResizes.size())
    {
        for (const auto& resize : m_texResizes)
        {
            if (m_boundTarget == resize.first)
                m_boundTarget = nullptr;
            resize.first->resize(m_ctx, resize.second.first, resize.second.second);
        }
        m_texResizes.clear();
        resetCommandBuffer();
        m_dynamicNeedsReset = true;
        m_resolveDispSource = nullptr;
        return;
    }

    m_drawBuf = m_fillBuf;
    m_fillBuf ^= 1;

    /* Queue the command buffer for execution */
    VkPipelineStageFlags pipeStageFlags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkSubmitInfo submitInfo = {};
    submitInfo.pNext = nullptr;
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 0;
    submitInfo.pWaitSemaphores = nullptr;
    submitInfo.pWaitDstStageMask = &pipeStageFlags;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_cmdBufs[m_drawBuf];
    submitInfo.signalSemaphoreCount = 0;
    submitInfo.pSignalSemaphores = nullptr;
    if (_resolveDisplay())
    {
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &m_swapChainReadySem;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &m_drawCompleteSem;
    }
    ThrowIfFailed(vk::EndCommandBuffer(m_cmdBufs[m_drawBuf]));
    ThrowIfFailed(vk::QueueSubmit(m_ctx->m_queue, 1, &submitInfo, m_drawCompleteFence));
    m_submitted = true;

    if (submitInfo.signalSemaphoreCount)
    {
        VulkanContext::Window::SwapChain& thisSc = m_windowCtx->m_swapChains[m_windowCtx->m_activeSwapChain];

        VkPresentInfoKHR present;
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.pNext = nullptr;
        present.swapchainCount = 1;
        present.pSwapchains = &thisSc.m_swapChain;
        present.pImageIndices = &thisSc.m_backBuf;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &m_drawCompleteSem;
        present.pResults = nullptr;

        ThrowIfFailed(vk::QueuePresentKHR(m_ctx->m_queue, &present));
    }

    resetCommandBuffer();
    resetDynamicCommandBuffer();
}

IGraphicsCommandQueue* _NewVulkanCommandQueue(VulkanContext* ctx, VulkanContext::Window* windowCtx,
                                              IGraphicsContext* parent)
{
    return new struct VulkanCommandQueue(ctx, windowCtx, parent);
}

IGraphicsDataFactory* _NewVulkanDataFactory(IGraphicsContext* parent, VulkanContext* ctx,
                                            uint32_t drawSamples)
{
    return new class VulkanDataFactoryImpl(parent, ctx, drawSamples);
}

}
