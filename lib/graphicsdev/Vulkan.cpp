#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#else
#define VK_USE_PLATFORM_XCB_KHR
#endif

#include "boo/graphicsdev/Vulkan.hpp"
#include "boo/IGraphicsContext.hpp"
#include <vector>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <SPIRV/disassemble.h>
#include "boo/graphicsdev/GLSLMacros.hpp"

#include "logvisor/logvisor.hpp"

#undef min
#undef max
#undef None

static TBuiltInResource DefaultBuiltInResource =
{
    32,
    6,
    32,
    32,
    64,
    4096,
    64,
    32,
    80,
    32,
    4096,
    32,
    128,
    8,
    16,
    16,
    15,
    -8,
    7,
    8,
    65535,
    65535,
    65535,
    1024,
    1024,
    64,
    1024,
    16,
    8,
    8,
    1,
    60,
    64,
    64,
    128,
    128,
    8,
    8,
    8,
    0,
    0,
    0,
    0,
    0,
    8,
    8,
    16,
    256,
    1024,
    1024,
    64,
    128,
    128,
    16,
    1024,
    4096,
    128,
    128,
    16,
    1024,
    120,
    32,
    64,
    16,
    0,
    0,
    0,
    0,
    8,
    8,
    1,
    0,
    0,
    0,
    0,
    1,
    1,
    16384,
    4,
    64,
    8,
    8,
    4,

    {
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1
    }
};

static void init_resources(TBuiltInResource &Resources) {
    Resources.maxLights = 32;
    Resources.maxClipPlanes = 6;
    Resources.maxTextureUnits = 32;
    Resources.maxTextureCoords = 32;
    Resources.maxVertexAttribs = 64;
    Resources.maxVertexUniformComponents = 4096;
    Resources.maxVaryingFloats = 64;
    Resources.maxVertexTextureImageUnits = 32;
    Resources.maxCombinedTextureImageUnits = 80;
    Resources.maxTextureImageUnits = 32;
    Resources.maxFragmentUniformComponents = 4096;
    Resources.maxDrawBuffers = 32;
    Resources.maxVertexUniformVectors = 128;
    Resources.maxVaryingVectors = 8;
    Resources.maxFragmentUniformVectors = 16;
    Resources.maxVertexOutputVectors = 16;
    Resources.maxFragmentInputVectors = 15;
    Resources.minProgramTexelOffset = -8;
    Resources.maxProgramTexelOffset = 7;
    Resources.maxClipDistances = 8;
    Resources.maxComputeWorkGroupCountX = 65535;
    Resources.maxComputeWorkGroupCountY = 65535;
    Resources.maxComputeWorkGroupCountZ = 65535;
    Resources.maxComputeWorkGroupSizeX = 1024;
    Resources.maxComputeWorkGroupSizeY = 1024;
    Resources.maxComputeWorkGroupSizeZ = 64;
    Resources.maxComputeUniformComponents = 1024;
    Resources.maxComputeTextureImageUnits = 16;
    Resources.maxComputeImageUniforms = 8;
    Resources.maxComputeAtomicCounters = 8;
    Resources.maxComputeAtomicCounterBuffers = 1;
    Resources.maxVaryingComponents = 60;
    Resources.maxVertexOutputComponents = 64;
    Resources.maxGeometryInputComponents = 64;
    Resources.maxGeometryOutputComponents = 128;
    Resources.maxFragmentInputComponents = 128;
    Resources.maxImageUnits = 8;
    Resources.maxCombinedImageUnitsAndFragmentOutputs = 8;
    Resources.maxCombinedShaderOutputResources = 8;
    Resources.maxImageSamples = 0;
    Resources.maxVertexImageUniforms = 0;
    Resources.maxTessControlImageUniforms = 0;
    Resources.maxTessEvaluationImageUniforms = 0;
    Resources.maxGeometryImageUniforms = 0;
    Resources.maxFragmentImageUniforms = 8;
    Resources.maxCombinedImageUniforms = 8;
    Resources.maxGeometryTextureImageUnits = 16;
    Resources.maxGeometryOutputVertices = 256;
    Resources.maxGeometryTotalOutputComponents = 1024;
    Resources.maxGeometryUniformComponents = 1024;
    Resources.maxGeometryVaryingComponents = 64;
    Resources.maxTessControlInputComponents = 128;
    Resources.maxTessControlOutputComponents = 128;
    Resources.maxTessControlTextureImageUnits = 16;
    Resources.maxTessControlUniformComponents = 1024;
    Resources.maxTessControlTotalOutputComponents = 4096;
    Resources.maxTessEvaluationInputComponents = 128;
    Resources.maxTessEvaluationOutputComponents = 128;
    Resources.maxTessEvaluationTextureImageUnits = 16;
    Resources.maxTessEvaluationUniformComponents = 1024;
    Resources.maxTessPatchComponents = 120;
    Resources.maxPatchVertices = 32;
    Resources.maxTessGenLevel = 64;
    Resources.maxViewports = 16;
    Resources.maxVertexAtomicCounters = 0;
    Resources.maxTessControlAtomicCounters = 0;
    Resources.maxTessEvaluationAtomicCounters = 0;
    Resources.maxGeometryAtomicCounters = 0;
    Resources.maxFragmentAtomicCounters = 8;
    Resources.maxCombinedAtomicCounters = 8;
    Resources.maxAtomicCounterBindings = 1;
    Resources.maxVertexAtomicCounterBuffers = 0;
    Resources.maxTessControlAtomicCounterBuffers = 0;
    Resources.maxTessEvaluationAtomicCounterBuffers = 0;
    Resources.maxGeometryAtomicCounterBuffers = 0;
    Resources.maxFragmentAtomicCounterBuffers = 1;
    Resources.maxCombinedAtomicCounterBuffers = 1;
    Resources.maxAtomicCounterBufferSize = 16384;
    Resources.maxTransformFeedbackBuffers = 4;
    Resources.maxTransformFeedbackInterleavedComponents = 64;
    Resources.maxCullDistances = 8;
    Resources.maxCombinedClipAndCullDistances = 8;
    Resources.maxSamples = 4;
    Resources.limits.nonInductiveForLoops = 1;
    Resources.limits.whileLoops = 1;
    Resources.limits.doWhileLoops = 1;
    Resources.limits.generalUniformIndexing = 1;
    Resources.limits.generalAttributeMatrixVectorIndexing = 1;
    Resources.limits.generalVaryingIndexing = 1;
    Resources.limits.generalSamplerIndexing = 1;
    Resources.limits.generalVariableIndexing = 1;
    Resources.limits.generalConstantMatrixVectorIndexing = 1;
}

namespace boo
{
static logvisor::Module Log("boo::Vulkan");
VulkanContext g_VulkanContext;

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
                           VkImageLayout new_image_layout)
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
    imageMemoryBarrier.subresourceRange.levelCount = 1;
    imageMemoryBarrier.subresourceRange.layerCount = 1;

    if (old_image_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        imageMemoryBarrier.srcAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }

    if (new_image_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        /* Make sure anything that was copying from this image has completed */
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    }

    if (new_image_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        /* Make sure any Copy or CPU writes to image are flushed */
        imageMemoryBarrier.srcAccessMask =
            VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }

    if (new_image_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        imageMemoryBarrier.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    }

    if (new_image_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        imageMemoryBarrier.dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    }

    VkPipelineStageFlags src_stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dest_stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    vkCmdPipelineBarrier(cmd, src_stages, dest_stages, 0, 0, NULL, 0, NULL,
                         1, &imageMemoryBarrier);
}

static VkResult InitGlobalExtensionProperties(VulkanContext::LayerProperties& layerProps) {
    VkExtensionProperties *instance_extensions;
    uint32_t instance_extension_count;
    VkResult res;
    char *layer_name = nullptr;

    layer_name = layerProps.properties.layerName;

    do {
        res = vkEnumerateInstanceExtensionProperties(
            layer_name, &instance_extension_count, nullptr);
        if (res)
            return res;

        if (instance_extension_count == 0) {
            return VK_SUCCESS;
        }

        layerProps.extensions.resize(instance_extension_count);
        instance_extensions = layerProps.extensions.data();
        res = vkEnumerateInstanceExtensionProperties(
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
    setenv("VK_LAYER_PATH", "/usr/share/vulkan/explicit_layer.d", 1);
    do {
        ThrowIfFailed(vkEnumerateInstanceLayerProperties(&instanceLayerCount, nullptr));

        if (instanceLayerCount == 0)
            break;

        vkProps = (VkLayerProperties *)realloc(vkProps, instanceLayerCount * sizeof(VkLayerProperties));

        res = vkEnumerateInstanceLayerProperties(&instanceLayerCount, vkProps);
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
    m_layerNames.push_back("VK_LAYER_LUNARG_object_tracker");
    m_layerNames.push_back("VK_LAYER_LUNARG_draw_state");
    m_layerNames.push_back("VK_LAYER_LUNARG_mem_tracker");
    m_layerNames.push_back("VK_LAYER_LUNARG_param_checker");
    m_layerNames.push_back("VK_LAYER_LUNARG_image");
    m_layerNames.push_back("VK_LAYER_LUNARG_threading");
    m_layerNames.push_back("VK_LAYER_LUNARG_swapchain");
    m_layerNames.push_back("VK_LAYER_LUNARG_device_limits");
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

    ThrowIfFailed(vkCreateInstance(&instInfo, nullptr, &m_instance));

#ifndef NDEBUG
    VkDebugReportCallbackEXT debugReportCallback;

    PFN_vkCreateDebugReportCallbackEXT createDebugReportCallback =
        (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(m_instance, "vkCreateDebugReportCallbackEXT");
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

    uint32_t gpuCount = 1;
    ThrowIfFailed(vkEnumeratePhysicalDevices(m_instance, &gpuCount, nullptr));
    assert(gpuCount);
    m_gpus.resize(gpuCount);

    ThrowIfFailed(vkEnumeratePhysicalDevices(m_instance, &gpuCount, m_gpus.data()));
    assert(gpuCount >= 1);

    vkGetPhysicalDeviceQueueFamilyProperties(m_gpus[0], &m_queueCount, nullptr);
    assert(m_queueCount >= 1);

    m_queueProps.resize(m_queueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_gpus[0], &m_queueCount, m_queueProps.data());
    assert(m_queueCount >= 1);

    /* This is as good a place as any to do this */
    vkGetPhysicalDeviceMemoryProperties(m_gpus[0], &m_memoryProperties);
    vkGetPhysicalDeviceProperties(m_gpus[0], &m_gpuProps);
}

void VulkanContext::initDevice()
{
    if (m_graphicsQueueFamilyIndex == UINT32_MAX)
        Log.report(logvisor::Fatal,
                   "VulkanContext::m_graphicsQueueFamilyIndex hasn't been initialized");

    /* create the device */
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

    ThrowIfFailed(vkCreateDevice(m_gpus[0], &deviceInfo, nullptr, &m_dev));
}

void VulkanContext::initSwapChain(VulkanContext::Window& windowCtx, VkSurfaceKHR surface, VkFormat format)
{
    VkSurfaceCapabilitiesKHR surfCapabilities;
    ThrowIfFailed(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_gpus[0], surface, &surfCapabilities));

    uint32_t presentModeCount;
    ThrowIfFailed(vkGetPhysicalDeviceSurfacePresentModesKHR(m_gpus[0], surface, &presentModeCount, nullptr));
    VkPresentModeKHR* presentModes = (VkPresentModeKHR*)malloc(presentModeCount * sizeof(VkPresentModeKHR));

    ThrowIfFailed(vkGetPhysicalDeviceSurfacePresentModesKHR(m_gpus[0], surface, &presentModeCount, presentModes));

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
    swapChainInfo.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    swapChainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapChainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapChainInfo.queueFamilyIndexCount = 0;
    swapChainInfo.pQueueFamilyIndices = nullptr;

    ThrowIfFailed(vkCreateSwapchainKHR(m_dev, &swapChainInfo, nullptr, &windowCtx.m_swapChain));

    uint32_t swapchainImageCount;
    ThrowIfFailed(vkGetSwapchainImagesKHR(m_dev, windowCtx.m_swapChain, &swapchainImageCount, nullptr));

    VkImage* swapchainImages = (VkImage*)malloc(swapchainImageCount * sizeof(VkImage));
    ThrowIfFailed(vkGetSwapchainImagesKHR(m_dev, windowCtx.m_swapChain, &swapchainImageCount, swapchainImages));

    windowCtx.m_bufs.resize(swapchainImageCount);

    // Going to need a command buffer to send the memory barriers in
    // set_image_layout but we couldn't have created one before we knew
    // what our graphics_queue_family_index is, but now that we have it,
    // create the command buffer

    VkCommandPoolCreateInfo cmdPoolInfo = {};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.pNext = nullptr;
    cmdPoolInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ThrowIfFailed(vkCreateCommandPool(m_dev, &cmdPoolInfo, nullptr, &m_loadPool));

    VkCommandBufferAllocateInfo cmd = {};
    cmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd.pNext = nullptr;
    cmd.commandPool = m_loadPool;
    cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd.commandBufferCount = 1;
    ThrowIfFailed(vkAllocateCommandBuffers(m_dev, &cmd, &m_loadCmdBuf));

    VkCommandBufferBeginInfo cmdBufBeginInfo = {};
    cmdBufBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ThrowIfFailed(vkBeginCommandBuffer(m_loadCmdBuf, &cmdBufBeginInfo));

    vkGetDeviceQueue(m_dev, m_graphicsQueueFamilyIndex, 0, &m_queue);

    for (uint32_t i=0 ; i<swapchainImageCount ; ++i)
    {
        VkImageViewCreateInfo colorImageView = {};
        colorImageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        colorImageView.pNext = nullptr;
        colorImageView.format = format;
        colorImageView.components.r = VK_COMPONENT_SWIZZLE_R;
        colorImageView.components.g = VK_COMPONENT_SWIZZLE_G;
        colorImageView.components.b = VK_COMPONENT_SWIZZLE_B;
        colorImageView.components.a = VK_COMPONENT_SWIZZLE_A;
        colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorImageView.subresourceRange.baseMipLevel = 0;
        colorImageView.subresourceRange.levelCount = 1;
        colorImageView.subresourceRange.baseArrayLayer = 0;
        colorImageView.subresourceRange.layerCount = 1;
        colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
        colorImageView.flags = 0;

        windowCtx.m_bufs[i].m_image = swapchainImages[i];

        SetImageLayout(m_loadCmdBuf, windowCtx.m_bufs[i].m_image, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        colorImageView.image = windowCtx.m_bufs[i].m_image;

        ThrowIfFailed(vkCreateImageView(m_dev, &colorImageView, nullptr, &windowCtx.m_bufs[i].m_view));
    }
    ThrowIfFailed(vkEndCommandBuffer(m_loadCmdBuf));

    VkFenceCreateInfo fenceInfo;
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    ThrowIfFailed(vkCreateFence(m_dev, &fenceInfo, nullptr, &m_loadFence));

    VkPipelineStageFlags pipeStageFlags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkSubmitInfo submitInfo[1] = {};
    submitInfo[0].pNext = nullptr;
    submitInfo[0].sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo[0].waitSemaphoreCount = 0;
    submitInfo[0].pWaitSemaphores = nullptr;
    submitInfo[0].pWaitDstStageMask = &pipeStageFlags;
    submitInfo[0].commandBufferCount = 1;
    submitInfo[0].pCommandBuffers = &m_loadCmdBuf;
    submitInfo[0].signalSemaphoreCount = 0;
    submitInfo[0].pSignalSemaphores = nullptr;

    ThrowIfFailed(vkQueueSubmit(m_queue, 1, submitInfo, m_loadFence));
    ThrowIfFailed(vkWaitForFences(m_dev, 1, &m_loadFence, VK_TRUE, -1));

    /* Reset fence and command buffer */
    ThrowIfFailed(vkResetFences(m_dev, 1, &m_loadFence));
    ThrowIfFailed(vkResetCommandBuffer(m_loadCmdBuf, 0));
    ThrowIfFailed(vkBeginCommandBuffer(m_loadCmdBuf, &cmdBufBeginInfo));
}

struct VulkanData : IGraphicsData
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
    VulkanData(VulkanContext* ctx) : m_ctx(ctx) {}
    ~VulkanData()
    {
        vkFreeMemory(m_ctx->m_dev, m_bufMem, nullptr);
        vkFreeMemory(m_ctx->m_dev, m_texMem, nullptr);
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
    VulkanGraphicsBufferS(BufferUse use, VulkanContext* ctx, const void* data, size_t stride, size_t count)
    : m_ctx(ctx), m_stride(stride), m_count(count), m_sz(stride * count),
      m_stagingBuf(new uint8_t[m_sz]), m_uniform(use == BufferUse::Uniform)
    {
        memcpy(m_stagingBuf.get(), data, m_sz);

        VkBufferCreateInfo bufInfo = {};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.pNext = nullptr;
        bufInfo.usage = USE_TABLE[int(use)];
        bufInfo.size = m_sz;
        bufInfo.queueFamilyIndexCount = 0;
        bufInfo.pQueueFamilyIndices = nullptr;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufInfo.flags = 0;
        ThrowIfFailed(vkCreateBuffer(ctx->m_dev, &bufInfo, nullptr, &m_bufferInfo.buffer));
    }
public:
    size_t size() const {return m_sz;}
    size_t m_stride;
    size_t m_count;
    VkDescriptorBufferInfo m_bufferInfo;
    bool m_uniform = false;
    ~VulkanGraphicsBufferS()
    {
        vkDestroyBuffer(m_ctx->m_dev, m_bufferInfo.buffer, nullptr);
    }

    VkDeviceSize sizeForGPU(VulkanContext* ctx, uint32_t& memTypeBits, VkDeviceSize offset)
    {
        if (m_uniform && ctx->m_gpuProps.limits.minUniformBufferOffsetAlignment)
        {
            offset = (offset +
                ctx->m_gpuProps.limits.minUniformBufferOffsetAlignment - 1) &
                ~(ctx->m_gpuProps.limits.minUniformBufferOffsetAlignment - 1);
        }

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(ctx->m_dev, m_bufferInfo.buffer, &memReqs);
        memTypeBits &= memReqs.memoryTypeBits;
        offset = (offset + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
        m_bufferInfo.offset = offset;

        offset += m_sz;
        m_bufferInfo.range = offset - m_bufferInfo.offset;

        return offset;
    }

    void placeForGPU(VulkanContext* ctx, VkDeviceMemory mem, uint8_t* buf)
    {
        memcpy(buf + m_bufferInfo.offset, m_stagingBuf.get(), m_sz);
        m_stagingBuf.reset();
        ThrowIfFailed(vkBindBufferMemory(ctx->m_dev, m_bufferInfo.buffer, mem, m_bufferInfo.offset));
    }
};

class VulkanGraphicsBufferD : public IGraphicsBufferD
{
    friend class VulkanDataFactory;
    friend struct VulkanCommandQueue;
    struct VulkanCommandQueue* m_q;
    std::unique_ptr<uint8_t[]> m_cpuBuf;
    size_t m_cpuSz;
    int m_validSlots = 0;
    VulkanGraphicsBufferD(VulkanCommandQueue* q, BufferUse use, VulkanContext* ctx, size_t stride, size_t count)
    : m_q(q), m_stride(stride), m_count(count), m_cpuSz(stride * count),
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
        ThrowIfFailed(vkCreateBuffer(ctx->m_dev, &bufInfo, nullptr, &m_bufferInfo[0].buffer));
        ThrowIfFailed(vkCreateBuffer(ctx->m_dev, &bufInfo, nullptr, &m_bufferInfo[1].buffer));
    }
    void update(int b);
public:
    size_t m_stride;
    size_t m_count;
    VkDeviceMemory m_mem;
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
            if (m_uniform && ctx->m_gpuProps.limits.minUniformBufferOffsetAlignment)
            {
                offset = (offset +
                    ctx->m_gpuProps.limits.minUniformBufferOffsetAlignment - 1) &
                    ~(ctx->m_gpuProps.limits.minUniformBufferOffsetAlignment - 1);
            }

            VkMemoryRequirements memReqs;
            vkGetBufferMemoryRequirements(ctx->m_dev, m_bufferInfo[i].buffer, &memReqs);
            memTypeBits &= memReqs.memoryTypeBits;
            offset = (offset + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
            m_bufferInfo[i].offset = offset;

            offset += memReqs.size;
            m_bufferInfo[i].range = offset - m_bufferInfo[i].offset;
        }

        return offset;
    }

    void placeForGPU(VulkanContext* ctx, VkDeviceMemory mem)
    {
        m_mem = mem;
        ThrowIfFailed(vkBindBufferMemory(ctx->m_dev, m_bufferInfo[0].buffer, mem, m_bufferInfo[0].offset));
        ThrowIfFailed(vkBindBufferMemory(ctx->m_dev, m_bufferInfo[1].buffer, mem, m_bufferInfo[1].offset));
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
    VulkanTextureS(VulkanContext* ctx, size_t width, size_t height, size_t mips,
                   TextureFormat fmt, const void* data, size_t sz)
    : m_ctx(ctx), m_fmt(fmt), m_sz(sz), m_width(width), m_height(height), m_mips(mips)
    {
        VkFormat pfmt;
        int pxPitchNum = 1;
        int pxPitchDenom = 1;
        switch (fmt)
        {
        case TextureFormat::RGBA8:
            pfmt = VK_FORMAT_R8G8B8A8_UNORM;
            pxPitchNum = 4;
            break;
        case TextureFormat::I8:
            pfmt = VK_FORMAT_R8_UNORM;
            break;
        case TextureFormat::DXT1:
            pfmt = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
            pxPitchNum = 1;
            pxPitchDenom = 2;
            break;
        default:
            Log.report(logvisor::Fatal, "unsupported tex format");
        }
        m_vkFmt = pfmt;

        /* create cpu image */
        VkImageCreateInfo texCreateInfo = {};
        texCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        texCreateInfo.pNext = nullptr;
        texCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        texCreateInfo.format = pfmt;
        texCreateInfo.extent.width = width;
        texCreateInfo.extent.height = height;
        texCreateInfo.extent.depth = 1;
        texCreateInfo.mipLevels = mips;
        texCreateInfo.arrayLayers = 1;
        texCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        texCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
        texCreateInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
        texCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        texCreateInfo.queueFamilyIndexCount = 0;
        texCreateInfo.pQueueFamilyIndices = nullptr;
        texCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        texCreateInfo.flags = 0;
        ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_cpuTex));

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(ctx->m_dev, m_cpuTex, &memReqs);

        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAlloc.pNext = nullptr;
        memAlloc.memoryTypeIndex = 0;
        memAlloc.allocationSize = memReqs.size;
        ThrowIfFalse(MemoryTypeFromProperties(ctx, memReqs.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                              &memAlloc.memoryTypeIndex));

        /* allocate memory */
        ThrowIfFailed(vkAllocateMemory(ctx->m_dev, &memAlloc, nullptr, &m_cpuMem));

        /* bind memory */
        ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_cpuTex, m_cpuMem, 0));

        /* map memory */
        uint8_t* mappedData;
        ThrowIfFailed(vkMapMemory(ctx->m_dev, m_cpuMem, 0, memReqs.size, 0, reinterpret_cast<void**>(&mappedData)));

        /* copy pitch-linear data */
        const uint8_t* srcDataIt = static_cast<const uint8_t*>(data);
        VkImageSubresource subres = {};
        subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subres.arrayLayer = 0;
        for (size_t i=0 ; i<mips ; ++i)
        {
            subres.mipLevel = i;
            VkSubresourceLayout layout;
            vkGetImageSubresourceLayout(ctx->m_dev, m_cpuTex, &subres, &layout);
            uint8_t* dstDataIt = static_cast<uint8_t*>(mappedData) + layout.offset;

            size_t srcRowPitch = width * pxPitchNum / pxPitchDenom;

            for (size_t y=0 ; y<height ; ++y)
            {
                memcpy(dstDataIt, srcDataIt, srcRowPitch);
                srcDataIt += srcRowPitch;
                dstDataIt += layout.rowPitch;
            }

            if (width > 1)
                width /= 2;
            if (height > 1)
                height /= 2;
        }

        /* flush to gpu */
        VkMappedMemoryRange mappedRange;
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.pNext = nullptr;
        mappedRange.memory = m_cpuMem;
        mappedRange.offset = 0;
        mappedRange.size = memReqs.size;
        ThrowIfFailed(vkFlushMappedMemoryRanges(ctx->m_dev, 1, &mappedRange));
        vkUnmapMemory(ctx->m_dev, m_cpuMem);

        /* create gpu image */
        texCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        texCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        texCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_gpuTex));

        /* create image view */
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.pNext = nullptr;
        viewInfo.image = m_gpuTex;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = pfmt;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mips;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        ThrowIfFailed(vkCreateImageView(ctx->m_dev, &viewInfo, nullptr, &m_gpuView));

        m_descInfo.sampler = ctx->m_linearSampler;
        m_descInfo.imageView = m_gpuView;
        m_descInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
public:
    VkImage m_cpuTex;
    VkDeviceMemory m_cpuMem;
    VkImage m_gpuTex;
    VkImageView m_gpuView;
    VkDescriptorImageInfo m_descInfo;
    VkDeviceSize m_gpuOffset;
    ~VulkanTextureS()
    {
        vkDestroyImageView(m_ctx->m_dev, m_gpuView, nullptr);
        vkDestroyImage(m_ctx->m_dev, m_cpuTex, nullptr);
        vkDestroyImage(m_ctx->m_dev, m_gpuTex, nullptr);
        vkFreeMemory(m_ctx->m_dev, m_cpuMem, nullptr);
    }

    void deleteUploadObjects()
    {
        vkDestroyImage(m_ctx->m_dev, m_cpuTex, nullptr);
        m_cpuTex = VK_NULL_HANDLE;
        vkFreeMemory(m_ctx->m_dev, m_cpuMem, nullptr);
        m_cpuMem = VK_NULL_HANDLE;
    }

    VkDeviceSize sizeForGPU(VulkanContext* ctx, uint32_t& memTypeBits, VkDeviceSize offset)
    {
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(ctx->m_dev, m_gpuTex, &memReqs);
        memTypeBits &= memReqs.memoryTypeBits;
        offset = (offset + memReqs.alignment - 1) & ~(memReqs.alignment - 1);

        m_gpuOffset = offset;
        offset += memReqs.size;

        return offset;
    }

    void placeForGPU(VulkanContext* ctx, VkDeviceMemory mem)
    {
        /* bind memory */
        ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_gpuTex, mem, m_gpuOffset));

        /* Since we're going to blit from the mappable image, set its layout to
         * SOURCE_OPTIMAL */
        SetImageLayout(ctx->m_loadCmdBuf, m_cpuTex, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_PREINITIALIZED,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        /* Since we're going to blit to the texture image, set its layout to
         * DESTINATION_OPTIMAL */
        SetImageLayout(ctx->m_loadCmdBuf, m_gpuTex, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageCopy copyRegions[16];
        size_t width = m_width;
        size_t height = m_height;
        size_t regionCount = std::min(size_t(16), m_mips);
        for (int i=0 ; i<regionCount ; ++i)
        {
            copyRegions[i].srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegions[i].srcSubresource.mipLevel = i;
            copyRegions[i].srcSubresource.baseArrayLayer = 0;
            copyRegions[i].srcSubresource.layerCount = 1;
            copyRegions[i].srcOffset.x = 0;
            copyRegions[i].srcOffset.y = 0;
            copyRegions[i].srcOffset.z = 0;
            copyRegions[i].dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegions[i].dstSubresource.mipLevel = i;
            copyRegions[i].dstSubresource.baseArrayLayer = 0;
            copyRegions[i].dstSubresource.layerCount = 1;
            copyRegions[i].dstOffset.x = 0;
            copyRegions[i].dstOffset.y = 0;
            copyRegions[i].dstOffset.z = 0;
            copyRegions[i].extent.width = width;
            copyRegions[i].extent.height = height;
            copyRegions[i].extent.depth = 1;

            if (width > 1)
                width /= 2;
            if (height > 1)
                height /= 2;
        }

        /* Put the copy command into the command buffer */
        vkCmdCopyImage(ctx->m_loadCmdBuf, m_cpuTex,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_gpuTex,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regionCount, copyRegions);

        /* Set the layout for the texture image from DESTINATION_OPTIMAL to
         * SHADER_READ_ONLY */
        SetImageLayout(ctx->m_loadCmdBuf, m_gpuTex, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    TextureFormat format() const {return m_fmt;}
};

class VulkanTextureSA : public ITextureSA
{
    friend class VulkanDataFactory;
    VulkanContext* m_ctx;
    TextureFormat m_fmt;
    size_t m_sz;
    size_t m_width, m_height, m_layers;
    VkFormat m_vkFmt;
    VulkanTextureSA(VulkanContext* ctx, size_t width, size_t height, size_t layers,
                   TextureFormat fmt, const void* data, size_t sz)
    : m_ctx(ctx), m_fmt(fmt), m_width(width), m_height(height), m_layers(layers), m_sz(sz)
    {
        VkFormat pfmt;
        int pxPitchNum = 1;
        int pxPitchDenom = 1;
        switch (fmt)
        {
        case TextureFormat::RGBA8:
            pfmt = VK_FORMAT_R8G8B8A8_UNORM;
            pxPitchNum = 4;
            break;
        case TextureFormat::I8:
            pfmt = VK_FORMAT_R8_UNORM;
            break;
        default:
            Log.report(logvisor::Fatal, "unsupported tex format");
        }
        m_vkFmt = pfmt;

        /* create cpu image */
        VkImageCreateInfo texCreateInfo = {};
        texCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        texCreateInfo.pNext = nullptr;
        texCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        texCreateInfo.format = pfmt;
        texCreateInfo.extent.width = width;
        texCreateInfo.extent.height = height;
        texCreateInfo.extent.depth = 1;
        texCreateInfo.mipLevels = 1;
        texCreateInfo.arrayLayers = layers;
        texCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        texCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
        texCreateInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
        texCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        texCreateInfo.queueFamilyIndexCount = 0;
        texCreateInfo.pQueueFamilyIndices = nullptr;
        texCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        texCreateInfo.flags = 0;
        ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_cpuTex));

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(ctx->m_dev, m_cpuTex, &memReqs);

        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAlloc.pNext = nullptr;
        memAlloc.memoryTypeIndex = 0;
        memAlloc.allocationSize = memReqs.size;
        ThrowIfFalse(MemoryTypeFromProperties(ctx, memReqs.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                              &memAlloc.memoryTypeIndex));

        /* allocate memory */
        ThrowIfFailed(vkAllocateMemory(ctx->m_dev, &memAlloc, nullptr, &m_cpuMem));

        /* bind memory */
        ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_cpuTex, m_cpuMem, 0));

        /* map memory */
        uint8_t* mappedData;
        ThrowIfFailed(vkMapMemory(ctx->m_dev, m_cpuMem, 0, memReqs.size, 0, reinterpret_cast<void**>(&mappedData)));

        /* copy pitch-linear data */
        const uint8_t* srcDataIt = static_cast<const uint8_t*>(data);
        VkImageSubresource subres = {};
        subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subres.mipLevel = 0;
        for (size_t i=0 ; i<layers ; ++i)
        {
            subres.arrayLayer = i;
            VkSubresourceLayout layout;
            vkGetImageSubresourceLayout(ctx->m_dev, m_cpuTex, &subres, &layout);
            uint8_t* dstDataIt = static_cast<uint8_t*>(mappedData) + layout.offset;

            size_t srcRowPitch = width * pxPitchNum / pxPitchDenom;

            for (size_t y=0 ; y<height ; ++y)
            {
                memcpy(dstDataIt, srcDataIt, srcRowPitch);
                srcDataIt += srcRowPitch;
                dstDataIt += layout.rowPitch;
            }
        }

        /* flush to gpu */
        VkMappedMemoryRange mappedRange;
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.pNext = nullptr;
        mappedRange.memory = m_cpuMem;
        mappedRange.offset = 0;
        mappedRange.size = memReqs.size;
        ThrowIfFailed(vkFlushMappedMemoryRanges(ctx->m_dev, 1, &mappedRange));
        vkUnmapMemory(ctx->m_dev, m_cpuMem);

        /* create gpu image */
        texCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        texCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        texCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_gpuTex));

        /* create image view */
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.pNext = nullptr;
        viewInfo.image = m_gpuTex;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = pfmt;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = layers;

        ThrowIfFailed(vkCreateImageView(ctx->m_dev, &viewInfo, nullptr, &m_gpuView));

        m_descInfo.sampler = ctx->m_linearSampler;
        m_descInfo.imageView = m_gpuView;
        m_descInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
public:
    VkImage m_cpuTex;
    VkDeviceMemory m_cpuMem;
    VkImage m_gpuTex;
    VkImageView m_gpuView;
    VkDescriptorImageInfo m_descInfo;
    VkDeviceSize m_gpuOffset;
    ~VulkanTextureSA()
    {
        vkDestroyImageView(m_ctx->m_dev, m_gpuView, nullptr);
        vkDestroyImage(m_ctx->m_dev, m_cpuTex, nullptr);
        vkDestroyImage(m_ctx->m_dev, m_gpuTex, nullptr);
        vkFreeMemory(m_ctx->m_dev, m_cpuMem, nullptr);
    }

    void deleteUploadObjects()
    {
        vkDestroyImage(m_ctx->m_dev, m_cpuTex, nullptr);
        m_cpuTex = VK_NULL_HANDLE;
        vkFreeMemory(m_ctx->m_dev, m_cpuMem, nullptr);
        m_cpuMem = VK_NULL_HANDLE;
    }

    VkDeviceSize sizeForGPU(VulkanContext* ctx, uint32_t& memTypeBits, VkDeviceSize offset)
    {
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(ctx->m_dev, m_gpuTex, &memReqs);
        memTypeBits &= memReqs.memoryTypeBits;
        offset = (offset + memReqs.alignment - 1) & ~(memReqs.alignment - 1);

        m_gpuOffset = offset;
        offset += memReqs.size;

        return offset;
    }

    void placeForGPU(VulkanContext* ctx, VkDeviceMemory mem)
    {
        /* bind memory */
        ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_gpuTex, mem, m_gpuOffset));

        /* Since we're going to blit from the mappable image, set its layout to
         * SOURCE_OPTIMAL */
        SetImageLayout(ctx->m_loadCmdBuf, m_cpuTex, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_PREINITIALIZED,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        /* Since we're going to blit to the texture image, set its layout to
         * DESTINATION_OPTIMAL */
        SetImageLayout(ctx->m_loadCmdBuf, m_gpuTex, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageCopy copyRegion;
        copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.srcSubresource.mipLevel = 0;
        copyRegion.srcSubresource.baseArrayLayer = 0;
        copyRegion.srcSubresource.layerCount = m_layers;
        copyRegion.srcOffset.x = 0;
        copyRegion.srcOffset.y = 0;
        copyRegion.srcOffset.z = 0;
        copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.dstSubresource.mipLevel = 0;
        copyRegion.dstSubresource.baseArrayLayer = 0;
        copyRegion.dstSubresource.layerCount = m_layers;
        copyRegion.dstOffset.x = 0;
        copyRegion.dstOffset.y = 0;
        copyRegion.dstOffset.z = 0;
        copyRegion.extent.width = m_width;
        copyRegion.extent.height = m_height;
        copyRegion.extent.depth = 1;

        /* Put the copy command into the command buffer */
        vkCmdCopyImage(ctx->m_loadCmdBuf, m_cpuTex,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_gpuTex,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        /* Set the layout for the texture image from DESTINATION_OPTIMAL to
         * SHADER_READ_ONLY */
        SetImageLayout(ctx->m_loadCmdBuf, m_gpuTex, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
    std::unique_ptr<uint8_t[]> m_cpuBuf;
    size_t m_cpuSz;
    VkDeviceSize m_srcRowPitch;
    VkDeviceSize m_cpuOffsets[2];
    int m_validSlots = 0;
    VulkanTextureD(VulkanCommandQueue* q, VulkanContext* ctx, size_t width, size_t height, TextureFormat fmt)
    : m_width(width), m_height(height), m_fmt(fmt), m_q(q)
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
        m_cpuBuf.reset(new uint8_t[m_cpuSz]);

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
        texCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
        texCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        texCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        texCreateInfo.queueFamilyIndexCount = 0;
        texCreateInfo.pQueueFamilyIndices = nullptr;
        texCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        texCreateInfo.flags = 0;

        /* create images and compute size for host-mappable images */
        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAlloc.pNext = nullptr;
        memAlloc.memoryTypeIndex = 0;
        memAlloc.allocationSize = 0;
        uint32_t memTypeBits = ~0;
        for (int i=0 ; i<2 ; ++i)
        {
            m_cpuOffsets[i] = memAlloc.allocationSize;

            /* create cpu image */
            ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_cpuTex[i]));
            m_cpuTexLayout[i] = VK_IMAGE_LAYOUT_UNDEFINED;

            VkMemoryRequirements memReqs;
            vkGetImageMemoryRequirements(ctx->m_dev, m_cpuTex[i], &memReqs);
            memAlloc.allocationSize += memReqs.size;
            memAlloc.allocationSize = (memAlloc.allocationSize + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
            memTypeBits &= memReqs.memoryTypeBits;

        }
        ThrowIfFalse(MemoryTypeFromProperties(ctx, memTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                              &memAlloc.memoryTypeIndex));

        /* allocate memory */
        ThrowIfFailed(vkAllocateMemory(ctx->m_dev, &memAlloc, nullptr, &m_cpuMem));

        texCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        texCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.pNext = nullptr;
        viewInfo.image = nullptr;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = pfmt;
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
            /* bind cpu memory */
            ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_cpuTex[i], m_cpuMem, m_cpuOffsets[i]));

            /* create gpu image */
            ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_gpuTex[i]));

            /* create image view */
            viewInfo.image = m_gpuTex[i];
            ThrowIfFailed(vkCreateImageView(ctx->m_dev, &viewInfo, nullptr, &m_gpuView[i]));

            m_descInfo[i].sampler = ctx->m_linearSampler;
            m_descInfo[i].imageView = m_gpuView[i];
            m_descInfo[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }
    void update(int b);
public:
    VkImageLayout m_cpuTexLayout[2];
    VkImage m_cpuTex[2];
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
            vkGetImageMemoryRequirements(ctx->m_dev, m_gpuTex[i], &memReqs);
            memTypeBits &= memReqs.memoryTypeBits;
            offset = (offset + memReqs.alignment - 1) & ~(memReqs.alignment - 1);

            m_gpuOffset[i] = offset;
            offset += memReqs.size;
        }

        return offset;
    }

    void placeForGPU(VulkanContext* ctx, VkDeviceMemory mem)
    {
        for (int i=0 ; i<2 ; ++i)
        {
            /* bind memory */
            ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_gpuTex[i], mem, m_gpuOffset[i]));
        }
    }

    TextureFormat format() const {return m_fmt;}
};

class VulkanTextureR : public ITextureR
{
    friend class VulkanDataFactory;
    friend struct VulkanCommandQueue;
    size_t m_width = 0;
    size_t m_height = 0;
    size_t m_samples = 0;

    bool m_enableShaderColorBinding;
    bool m_enableShaderDepthBinding;

    void Setup(VulkanContext* ctx, size_t width, size_t height, size_t samples,
               bool enableShaderColorBinding, bool enableShaderDepthBinding)
    {
        /* no-ops on first call */
        doDestroy();

        /* color target */
        VkImageCreateInfo texCreateInfo = {};
        texCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        texCreateInfo.pNext = nullptr;
        texCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        texCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
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
        ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_colorTex));

        VkImageViewCreateInfo viewCreateInfo = {};
        viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCreateInfo.pNext = nullptr;
        viewCreateInfo.image = m_colorTex;
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCreateInfo.subresourceRange.baseMipLevel = 0;
        viewCreateInfo.subresourceRange.levelCount = 1;
        viewCreateInfo.subresourceRange.baseArrayLayer = 0;
        viewCreateInfo.subresourceRange.layerCount = 1;
        ThrowIfFailed(vkCreateImageView(ctx->m_dev, &viewCreateInfo, nullptr, &m_colorView));

        /* depth target */
        texCreateInfo.format = VK_FORMAT_D24_UNORM_S8_UINT;
        texCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        viewCreateInfo.format = VK_FORMAT_D24_UNORM_S8_UINT;
        viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_depthTex));
        ThrowIfFailed(vkCreateImageView(ctx->m_dev, &viewCreateInfo, nullptr, &m_depthView));

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
        ThrowIfFailed(vkCreateFramebuffer(ctx->m_dev, &fbCreateInfo, nullptr, &m_framebuffer));

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

        /* tally total memory requirements */
        VkMemoryRequirements memReqs;
        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAlloc.pNext = nullptr;
        memAlloc.memoryTypeIndex = 0;
        memAlloc.allocationSize = 0;
        uint32_t memTypeBits = ~0;

        VkDeviceSize gpuOffsets[4];

        vkGetImageMemoryRequirements(ctx->m_dev, m_colorTex, &memReqs);
        gpuOffsets[0] = memAlloc.allocationSize;
        memAlloc.allocationSize += memReqs.size;
        memAlloc.allocationSize = (memAlloc.allocationSize + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
        memTypeBits &= memReqs.memoryTypeBits;

        vkGetImageMemoryRequirements(ctx->m_dev, m_depthTex, &memReqs);
        gpuOffsets[1] = memAlloc.allocationSize;
        memAlloc.allocationSize += memReqs.size;
        memAlloc.allocationSize = (memAlloc.allocationSize + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
        memTypeBits &= memReqs.memoryTypeBits;

        if (enableShaderColorBinding)
        {
            texCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            texCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            viewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_colorBindTex));
            ThrowIfFailed(vkCreateImageView(ctx->m_dev, &viewCreateInfo, nullptr, &m_colorBindView));

            vkGetImageMemoryRequirements(ctx->m_dev, m_colorBindTex, &memReqs);
            gpuOffsets[2] = memAlloc.allocationSize;
            memAlloc.allocationSize += memReqs.size;
            memAlloc.allocationSize = (memAlloc.allocationSize + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
            memTypeBits &= memReqs.memoryTypeBits;

            m_colorBindDescInfo.sampler = ctx->m_linearSampler;
            m_colorBindDescInfo.imageView = m_colorBindView;
            m_colorBindDescInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        if (enableShaderDepthBinding)
        {
            texCreateInfo.format = VK_FORMAT_D24_UNORM_S8_UINT;
            texCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            viewCreateInfo.format = VK_FORMAT_D24_UNORM_S8_UINT;
            viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_depthBindTex));
            ThrowIfFailed(vkCreateImageView(ctx->m_dev, &viewCreateInfo, nullptr, &m_depthBindView));

            vkGetImageMemoryRequirements(ctx->m_dev, m_depthBindTex, &memReqs);
            gpuOffsets[3] = memAlloc.allocationSize;
            memAlloc.allocationSize += memReqs.size;
            memAlloc.allocationSize = (memAlloc.allocationSize + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
            memTypeBits &= memReqs.memoryTypeBits;

            m_depthBindDescInfo.sampler = ctx->m_linearSampler;
            m_depthBindDescInfo.imageView = m_depthBindView;
            m_depthBindDescInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        ThrowIfFalse(MemoryTypeFromProperties(ctx, memTypeBits, 0, &memAlloc.memoryTypeIndex));

        /* allocate memory */
        ThrowIfFailed(vkAllocateMemory(ctx->m_dev, &memAlloc, nullptr, &m_gpuMem));

        /* bind memory */
        ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_colorTex, m_gpuMem, gpuOffsets[0]));
        ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_depthTex, m_gpuMem, gpuOffsets[1]));
        if (enableShaderColorBinding)
            ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_colorBindTex, m_gpuMem, gpuOffsets[2]));
        if (enableShaderDepthBinding)
            ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_depthBindTex, m_gpuMem, gpuOffsets[3]));
    }

    VulkanCommandQueue* m_q;
    VulkanTextureR(VulkanContext* ctx, VulkanCommandQueue* q, size_t width, size_t height, size_t samples,
                   bool enableShaderColorBinding, bool enableShaderDepthBinding)
    : m_q(q), m_width(width), m_height(height), m_samples(samples),
      m_enableShaderColorBinding(enableShaderColorBinding),
      m_enableShaderDepthBinding(enableShaderDepthBinding)
    {
        if (samples == 0) m_samples = 1;
        Setup(ctx, width, height, samples, enableShaderColorBinding, enableShaderDepthBinding);
    }
public:
    size_t samples() const {return m_samples;}
    VkDeviceMemory m_gpuMem = VK_NULL_HANDLE;

    VkImage m_colorTex = VK_NULL_HANDLE;
    VkImageView m_colorView = VK_NULL_HANDLE;

    VkImage m_depthTex = VK_NULL_HANDLE;
    VkImageView m_depthView = VK_NULL_HANDLE;

    VkImage m_colorBindTex = VK_NULL_HANDLE;
    VkImageView m_colorBindView = VK_NULL_HANDLE;
    VkDescriptorImageInfo m_colorBindDescInfo;

    VkImage m_depthBindTex = VK_NULL_HANDLE;
    VkImageView m_depthBindView = VK_NULL_HANDLE;
    VkDescriptorImageInfo m_depthBindDescInfo;

    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkRenderPassBeginInfo m_passBeginInfo;

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
        Setup(ctx, width, height, m_samples, m_enableShaderColorBinding, m_enableShaderDepthBinding);
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
    VulkanVertexFormat(size_t elementCount, const VertexElementDescriptor* elements)
    : m_attributes(new VkVertexInputAttributeDescription[elementCount])
    {
        m_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        m_info.pNext = nullptr;
        m_info.flags = 0;
        m_info.vertexBindingDescriptionCount = 1;
        m_info.pVertexBindingDescriptions = m_bindings;
        m_info.vertexAttributeDescriptionCount = elementCount;
        m_info.pVertexAttributeDescriptions = m_attributes.get();

        size_t offset = 0;
        size_t instOffset = 0;
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
                attribute.offset = instOffset;
                instOffset += SEMANTIC_SIZE_TABLE[semantic];
            }
            else
            {
                attribute.binding = 0;
                attribute.offset = offset;
                offset += SEMANTIC_SIZE_TABLE[semantic];
            }
        }

        m_bindings[0].binding = 1;
        m_bindings[0].stride = offset;
        m_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        if (instOffset)
        {
            m_info.vertexBindingDescriptionCount = 2;
            m_bindings[1].binding = 1;
            m_bindings[1].stride = instOffset;
            m_bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
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
    VulkanContext* m_ctx;
    VulkanShaderPipeline(VulkanContext* ctx,
                         VkShaderModule vert,
                         VkShaderModule frag,
                         VkPipelineCache pipelineCache,
                         const VulkanVertexFormat* vtxFmt,
                         BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                         bool depthTest, bool depthWrite, bool backfaceCulling)
    : m_ctx(ctx)
    {
        VkDynamicState dynamicStateEnables[VK_DYNAMIC_STATE_RANGE_SIZE];
        memset(dynamicStateEnables, 0, sizeof(dynamicStateEnables));
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
        stages[0].module = vert;
        stages[0].pName = "main";
        stages[0].pSpecializationInfo = nullptr;

        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].pNext = nullptr;
        stages[1].flags = 0;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
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
        rasterizationInfo.depthClampEnable = VK_TRUE;
        rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;
        rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizationInfo.cullMode = backfaceCulling ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
        rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizationInfo.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampleInfo = {};
        multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleInfo.pNext = nullptr;
        multisampleInfo.flags = 0;
        multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {};
        depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilInfo.pNext = nullptr;
        depthStencilInfo.flags = 0;
        depthStencilInfo.depthTestEnable = depthTest;
        depthStencilInfo.depthWriteEnable = depthWrite;
        depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
        depthStencilInfo.back.compareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineColorBlendAttachmentState colorAttachment = {};
        colorAttachment.blendEnable = dstFac != BlendFactor::Zero;
        colorAttachment.srcColorBlendFactor = BLEND_FACTOR_TABLE[int(srcFac)];
        colorAttachment.dstColorBlendFactor = BLEND_FACTOR_TABLE[int(dstFac)];
        colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorAttachment.srcAlphaBlendFactor = BLEND_FACTOR_TABLE[int(srcFac)];
        colorAttachment.dstAlphaBlendFactor = BLEND_FACTOR_TABLE[int(dstFac)];
        colorAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        colorAttachment.colorWriteMask = 0xf;

        VkPipelineColorBlendStateCreateInfo colorBlendInfo = {};
        colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendInfo.pNext = nullptr;
        colorBlendInfo.flags = 0;
        colorBlendInfo.logicOpEnable = VK_FALSE;
        colorBlendInfo.logicOp = VK_LOGIC_OP_NO_OP;
        colorBlendInfo.attachmentCount = 1;
        colorBlendInfo.pAttachments = &colorAttachment;
        colorBlendInfo.blendConstants[0] = 1.f;
        colorBlendInfo.blendConstants[1] = 1.f;
        colorBlendInfo.blendConstants[2] = 1.f;
        colorBlendInfo.blendConstants[3] = 1.f;

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

        ThrowIfFailed(vkCreateGraphicsPipelines(ctx->m_dev, pipelineCache, 1, &pipelineCreateInfo,
                                                nullptr, &m_pipeline));
    }
public:
    VkPipeline m_pipeline;
    ~VulkanShaderPipeline()
    {
        vkDestroyPipeline(m_ctx->m_dev, m_pipeline, nullptr);
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

static const VkDescriptorImageInfo* GetTextureGPUResource(const ITexture* tex, int idx)
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
        return &ctex->m_colorBindDescInfo;
    }
    default: break;
    }
    return nullptr;
}

struct VulkanShaderDataBinding : IShaderDataBinding
{
    VulkanContext* m_ctx;
    VulkanShaderPipeline* m_pipeline;
    IGraphicsBuffer* m_vbuf;
    IGraphicsBuffer* m_instVbuf;
    IGraphicsBuffer* m_ibuf;
    size_t m_ubufCount;
    std::unique_ptr<IGraphicsBuffer*[]> m_ubufs;
    std::vector<VkDescriptorBufferInfo> m_ubufOffs;
    size_t m_texCount;
    std::unique_ptr<ITexture*[]> m_texs;

    VkBuffer m_vboBufs[2][2] = {{},{}};
    VkDeviceSize m_vboOffs[2][2] = {{},{}};
    VkBuffer m_iboBufs[2] = {};
    VkDeviceSize m_iboOffs[2] = {};

    VkDescriptorPool m_descPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descSets[2];

#ifndef NDEBUG
    /* Debugging aids */
    bool m_committed = false;
#endif

    VulkanShaderDataBinding(VulkanContext* ctx,
                            IShaderPipeline* pipeline,
                            IGraphicsBuffer* vbuf, IGraphicsBuffer* instVbuf, IGraphicsBuffer* ibuf,
                            size_t ubufCount, IGraphicsBuffer** ubufs,
                            const size_t* ubufOffs, const size_t* ubufSizes,
                            size_t texCount, ITexture** texs)
    : m_ctx(ctx),
      m_pipeline(static_cast<VulkanShaderPipeline*>(pipeline)),
      m_vbuf(vbuf),
      m_instVbuf(instVbuf),
      m_ibuf(ibuf),
      m_ubufCount(ubufCount),
      m_ubufs(new IGraphicsBuffer*[ubufCount]),
      m_texCount(texCount),
      m_texs(new ITexture*[texCount])
    {
        if (ubufOffs && ubufSizes)
        {
            m_ubufOffs.reserve(ubufCount);
            for (size_t i=0 ; i<ubufCount ; ++i)
            {
#ifndef NDEBUG
                if (ubufOffs[i] % 256)
                    Log.report(logvisor::Fatal, "non-256-byte-aligned uniform-offset %d provided to newShaderDataBinding", int(i));
#endif
                m_ubufOffs.push_back({VK_NULL_HANDLE, ubufOffs[i], (ubufSizes[i] + 255) & ~255});
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
#ifndef NDEBUG
            if (!texs[i])
                Log.report(logvisor::Fatal, "null texture %d provided to newShaderDataBinding", int(i));
#endif
            m_texs[i] = texs[i];
        }

        size_t totalDescs = ubufCount + texCount;
        if (totalDescs > 0)
        {
            VkDescriptorPoolSize poolSizes[2] = {};
            VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
            descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descriptorPoolInfo.pNext = nullptr;
            descriptorPoolInfo.maxSets = 2;
            descriptorPoolInfo.poolSizeCount = 0;
            descriptorPoolInfo.pPoolSizes = poolSizes;

            if (ubufCount)
            {
                poolSizes[descriptorPoolInfo.poolSizeCount].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                poolSizes[descriptorPoolInfo.poolSizeCount].descriptorCount = ubufCount;
                ++descriptorPoolInfo.poolSizeCount;
            }

            if (texCount)
            {
                poolSizes[descriptorPoolInfo.poolSizeCount].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                poolSizes[descriptorPoolInfo.poolSizeCount].descriptorCount = texCount;
                ++descriptorPoolInfo.poolSizeCount;
            }
            ThrowIfFailed(vkCreateDescriptorPool(ctx->m_dev, &descriptorPoolInfo, nullptr, &m_descPool));

            VkDescriptorSetLayout layouts[] = {ctx->m_descSetLayout, ctx->m_descSetLayout};
            VkDescriptorSetAllocateInfo descAllocInfo;
            descAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descAllocInfo.pNext = nullptr;
            descAllocInfo.descriptorPool = m_descPool;
            descAllocInfo.descriptorSetCount = 2;
            descAllocInfo.pSetLayouts = layouts;
            ThrowIfFailed(vkAllocateDescriptorSets(ctx->m_dev, &descAllocInfo, m_descSets));
        }
    }

    ~VulkanShaderDataBinding()
    {
        vkDestroyDescriptorPool(m_ctx->m_dev, m_descPool, nullptr);
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
                m_vboOffs[b][0] = vbufInfo->offset;
            }
            if (m_instVbuf)
            {
                const VkDescriptorBufferInfo* vbufInfo = GetBufferGPUResource(m_instVbuf, b);
                m_vboBufs[b][1] = vbufInfo->buffer;
                m_vboOffs[b][1] = vbufInfo->offset;
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
                        writes[totalWrites].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        writes[totalWrites].pNext = nullptr;
                        writes[totalWrites].dstSet = m_descSets[b];
                        writes[totalWrites].descriptorCount = 1;
                        writes[totalWrites].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                        const VkDescriptorBufferInfo* origInfo = GetBufferGPUResource(m_ubufs[i], b);
                        VkDescriptorBufferInfo& modInfo = m_ubufOffs[i];
                        modInfo.buffer = origInfo->buffer;
                        modInfo.offset += origInfo->offset;
                        writes[totalWrites].pBufferInfo = &modInfo;
                        writes[totalWrites].dstArrayElement = 0;
                        writes[totalWrites].dstBinding = binding;
                        ++totalWrites;
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
                        writes[totalWrites].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
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
                if (i<m_texCount)
                {
                    writes[totalWrites].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[totalWrites].pNext = nullptr;
                    writes[totalWrites].dstSet = m_descSets[b];
                    writes[totalWrites].descriptorCount = 1;
                    writes[totalWrites].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    writes[totalWrites].pImageInfo = GetTextureGPUResource(m_texs[i], b);
                    writes[totalWrites].dstArrayElement = 0;
                    writes[totalWrites].dstBinding = binding;
                    ++totalWrites;
                }
                ++binding;
            }
        }
        if (totalWrites)
            vkUpdateDescriptorSets(ctx->m_dev, totalWrites, writes, 0, nullptr);

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

        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->m_pipeline);
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ctx->m_pipelinelayout, 0, 1, &m_descSets[b], 0, nullptr);
        vkCmdBindVertexBuffers(cmdBuf, 0, 2, m_vboBufs[b], m_vboOffs[b]);
        if (m_ibuf)
            vkCmdBindIndexBuffer(cmdBuf, m_iboBufs[b], m_iboOffs[b], VK_INDEX_TYPE_UINT32);
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
    VkSemaphore m_swapChainReadySem;
    VkSemaphore m_drawCompleteSem;
    VkFence m_drawCompleteFence;

    VkCommandPool m_dynamicCmdPool;
    VkCommandBuffer m_dynamicCmdBufs[2];
    VkFence m_dynamicBufFence;

    bool m_running = true;
    bool m_dynamicNeedsReset = false;

    size_t m_fillBuf = 0;
    size_t m_drawBuf = 0;

    void resetCommandBuffer()
    {
        ThrowIfFailed(vkResetCommandBuffer(m_cmdBufs[m_fillBuf], 0));
    }

    void resetDynamicCommandBuffer()
    {
        ThrowIfFailed(vkResetCommandBuffer(m_dynamicCmdBufs[m_fillBuf], 0));
        m_dynamicNeedsReset = false;
    }

    void stallDynamicUpload()
    {
        if (m_dynamicNeedsReset)
        {
            ThrowIfFailed(vkWaitForFences(m_ctx->m_dev, 1, &m_dynamicBufFence, VK_FALSE, -1));
            resetDynamicCommandBuffer();
        }
    }

    VulkanCommandQueue(VulkanContext* ctx, VulkanContext::Window* windowCtx, IGraphicsContext* parent)
    : m_ctx(ctx), m_windowCtx(windowCtx), m_parent(parent)
    {
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = m_ctx->m_graphicsQueueFamilyIndex;
        ThrowIfFailed(vkCreateCommandPool(ctx->m_dev, &poolInfo, nullptr, &m_cmdPool));
        ThrowIfFailed(vkCreateCommandPool(ctx->m_dev, &poolInfo, nullptr, &m_dynamicCmdPool));

        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_cmdPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 2;
        ThrowIfFailed(vkAllocateCommandBuffers(m_ctx->m_dev, &allocInfo, m_cmdBufs));

        VkCommandBufferAllocateInfo dynAllocInfo =
        {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            nullptr,
            m_dynamicCmdPool,
            VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            2
        };
        ThrowIfFailed(vkAllocateCommandBuffers(m_ctx->m_dev, &dynAllocInfo, m_dynamicCmdBufs));

        VkSemaphoreCreateInfo semInfo = {};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        ThrowIfFailed(vkCreateSemaphore(ctx->m_dev, &semInfo, nullptr, &m_swapChainReadySem));
        ThrowIfFailed(vkCreateSemaphore(ctx->m_dev, &semInfo, nullptr, &m_drawCompleteSem));

        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        ThrowIfFailed(vkCreateFence(m_ctx->m_dev, &fenceInfo, nullptr, &m_drawCompleteFence));
        ThrowIfFailed(vkCreateFence(m_ctx->m_dev, &fenceInfo, nullptr, &m_dynamicBufFence));
    }

    void stopRenderer()
    {
        m_running = false;
        vkWaitForFences(m_ctx->m_dev, 1, &m_drawCompleteFence, VK_FALSE, -1);
    }

    ~VulkanCommandQueue()
    {
        if (m_running)
            stopRenderer();

        vkDestroyFence(m_ctx->m_dev, m_dynamicBufFence, nullptr);
        vkDestroyFence(m_ctx->m_dev, m_drawCompleteFence, nullptr);
        vkDestroySemaphore(m_ctx->m_dev, m_drawCompleteSem, nullptr);
        vkDestroySemaphore(m_ctx->m_dev, m_swapChainReadySem, nullptr);
        vkDestroyCommandPool(m_ctx->m_dev, m_dynamicCmdPool, nullptr);
        vkDestroyCommandPool(m_ctx->m_dev, m_cmdPool, nullptr);
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

        if (m_boundTarget)
        {
            SetImageLayout(cmdBuf, m_boundTarget->m_colorTex, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            SetImageLayout(cmdBuf, m_boundTarget->m_depthTex, VK_IMAGE_ASPECT_DEPTH_BIT,
                           VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        }

        SetImageLayout(cmdBuf, ctarget->m_colorTex, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        SetImageLayout(cmdBuf, ctarget->m_depthTex, VK_IMAGE_ASPECT_DEPTH_BIT,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        vkCmdBeginRenderPass(cmdBuf, &ctarget->m_passBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        m_boundTarget = ctarget;
    }

    void setViewport(const SWindowRect& rect)
    {
        if (m_boundTarget)
        {
            VkViewport vp = {float(rect.location[0]), float(m_boundTarget->m_height - rect.location[1]),
                             float(rect.size[0]), float(rect.size[1]), 0.0f, 1.0f};
            vkCmdSetViewport(m_cmdBufs[m_fillBuf], 0, 1, &vp);
        }
    }

    void setScissor(const SWindowRect& rect)
    {
        if (m_boundTarget)
        {
            VkRect2D vkrect =
            {
                {int32_t(rect.location[0]), int32_t(m_boundTarget->m_height) - int32_t(rect.location[1])},
                {uint32_t(rect.size[0]), uint32_t(rect.size[1])}
            };
            vkCmdSetScissor(m_cmdBufs[m_fillBuf], 0, 1, &vkrect);
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

    float m_clearColor[4] = {0.0,0.0,0.0,1.0};
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
        setRenderTarget(m_boundTarget);
    }

    void draw(size_t start, size_t count)
    {
        vkCmdDraw(m_cmdBufs[m_fillBuf], count, 1, start, 0);
    }

    void drawIndexed(size_t start, size_t count)
    {
        vkCmdDrawIndexed(m_cmdBufs[m_fillBuf], count, 1, start, 0, 0);
    }

    void drawInstances(size_t start, size_t count, size_t instCount)
    {
        vkCmdDraw(m_cmdBufs[m_fillBuf], count, instCount, start, 0);
    }

    void drawInstancesIndexed(size_t start, size_t count, size_t instCount)
    {
        vkCmdDrawIndexed(m_cmdBufs[m_fillBuf], count, instCount, start, 0, 0);
    }

    bool m_doPresent = false;
    void resolveDisplay(ITextureR* source)
    {
        VkCommandBuffer cmdBuf = m_cmdBufs[m_fillBuf];
        VulkanTextureR* csource = static_cast<VulkanTextureR*>(source);

        ThrowIfFailed(vkAcquireNextImageKHR(m_ctx->m_dev, m_windowCtx->m_swapChain, UINT64_MAX,
                                            m_swapChainReadySem, nullptr, &m_windowCtx->m_backBuf));
        VulkanContext::Window::Buffer& dest = m_windowCtx->m_bufs[m_windowCtx->m_backBuf];

        if (source == m_boundTarget)
            SetImageLayout(cmdBuf, csource->m_colorTex, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

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
            vkCmdResolveImage(cmdBuf,
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
            vkCmdCopyImage(cmdBuf,
                           csource->m_colorTex, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           dest.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyInfo);
        }

        if (source == m_boundTarget)
            SetImageLayout(cmdBuf, csource->m_colorTex, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        m_doPresent = true;
    }

    void resolveBindTexture(ITextureR* texture, const SWindowRect& rect, bool tlOrigin, bool color, bool depth)
    {
        VkCommandBuffer cmdBuf = m_cmdBufs[m_fillBuf];
        VulkanTextureR* ctexture = static_cast<VulkanTextureR*>(texture);

        if (color && ctexture->m_enableShaderColorBinding)
        {
            if (ctexture == m_boundTarget)
                SetImageLayout(cmdBuf, ctexture->m_colorTex, VK_IMAGE_ASPECT_COLOR_BIT,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            SetImageLayout(cmdBuf, ctexture->m_colorBindTex, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkImageCopy copyInfo = {};
            copyInfo.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyInfo.srcSubresource.mipLevel = 0;
            copyInfo.srcSubresource.baseArrayLayer = 0;
            copyInfo.srcSubresource.layerCount = 1;
            copyInfo.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyInfo.dstSubresource.mipLevel = 0;
            copyInfo.dstSubresource.baseArrayLayer = 0;
            copyInfo.dstSubresource.layerCount = 1;
            copyInfo.srcOffset.x = rect.location[0];
            if (tlOrigin)
                copyInfo.srcOffset.y = rect.location[1];
            else
                copyInfo.srcOffset.y = ctexture->m_height - rect.location[1] - rect.size[1];
            copyInfo.dstOffset = copyInfo.srcOffset;
            copyInfo.extent.width = ctexture->m_width;
            copyInfo.extent.height = ctexture->m_height;
            copyInfo.extent.depth = 1;
            vkCmdCopyImage(cmdBuf,
                           ctexture->m_colorTex, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           ctexture->m_colorBindTex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyInfo);

            if (ctexture == m_boundTarget)
                SetImageLayout(cmdBuf, ctexture->m_colorTex, VK_IMAGE_ASPECT_COLOR_BIT,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

            SetImageLayout(cmdBuf, ctexture->m_colorBindTex, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        if (depth && ctexture->m_enableShaderDepthBinding)
        {
            if (ctexture == m_boundTarget)
                SetImageLayout(cmdBuf, ctexture->m_depthTex, VK_IMAGE_ASPECT_DEPTH_BIT,
                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            SetImageLayout(cmdBuf, ctexture->m_depthBindTex, VK_IMAGE_ASPECT_DEPTH_BIT,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkImageCopy copyInfo = {};
            copyInfo.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            copyInfo.srcSubresource.mipLevel = 0;
            copyInfo.srcSubresource.baseArrayLayer = 0;
            copyInfo.srcSubresource.layerCount = 1;
            copyInfo.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            copyInfo.dstSubresource.mipLevel = 0;
            copyInfo.dstSubresource.baseArrayLayer = 0;
            copyInfo.dstSubresource.layerCount = 1;
            copyInfo.srcOffset.x = rect.location[0];
            if (tlOrigin)
                copyInfo.srcOffset.y = rect.location[1];
            else
                copyInfo.srcOffset.y = ctexture->m_height - rect.location[1] - rect.size[1];
            copyInfo.dstOffset = copyInfo.srcOffset;
            copyInfo.extent.width = ctexture->m_width;
            copyInfo.extent.height = ctexture->m_height;
            copyInfo.extent.depth = 1;
            vkCmdCopyImage(cmdBuf,
                           ctexture->m_depthTex, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           ctexture->m_depthBindTex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyInfo);

            if (ctexture == m_boundTarget)
                SetImageLayout(cmdBuf, ctexture->m_depthTex, VK_IMAGE_ASPECT_DEPTH_BIT,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

            SetImageLayout(cmdBuf, ctexture->m_depthBindTex, VK_IMAGE_ASPECT_DEPTH_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    void execute();
};

VulkanGraphicsBufferD::~VulkanGraphicsBufferD()
{
    vkDestroyBuffer(m_q->m_ctx->m_dev, m_bufferInfo[0].buffer, nullptr);
    vkDestroyBuffer(m_q->m_ctx->m_dev, m_bufferInfo[1].buffer, nullptr);
}

VulkanTextureD::~VulkanTextureD()
{
    vkDestroyImageView(m_q->m_ctx->m_dev, m_gpuView[0], nullptr);
    vkDestroyImageView(m_q->m_ctx->m_dev, m_gpuView[1], nullptr);
    vkDestroyImage(m_q->m_ctx->m_dev, m_cpuTex[0], nullptr);
    vkDestroyImage(m_q->m_ctx->m_dev, m_cpuTex[1], nullptr);
    vkDestroyImage(m_q->m_ctx->m_dev, m_gpuTex[0], nullptr);
    vkDestroyImage(m_q->m_ctx->m_dev, m_gpuTex[1], nullptr);
    vkFreeMemory(m_q->m_ctx->m_dev, m_cpuMem, nullptr);
}

void VulkanTextureR::doDestroy()
{
    vkDestroyFramebuffer(m_q->m_ctx->m_dev, m_framebuffer, nullptr);
    m_framebuffer = VK_NULL_HANDLE;
    vkDestroyImageView(m_q->m_ctx->m_dev, m_colorView, nullptr);
    m_colorView = VK_NULL_HANDLE;
    vkDestroyImage(m_q->m_ctx->m_dev, m_colorTex, nullptr);
    m_colorTex = VK_NULL_HANDLE;
    vkDestroyImageView(m_q->m_ctx->m_dev, m_depthView, nullptr);
    m_depthView = VK_NULL_HANDLE;
    vkDestroyImage(m_q->m_ctx->m_dev, m_depthTex, nullptr);
    m_depthTex = VK_NULL_HANDLE;
    vkDestroyImageView(m_q->m_ctx->m_dev, m_colorBindView, nullptr);
    m_colorBindView = VK_NULL_HANDLE;
    vkDestroyImage(m_q->m_ctx->m_dev, m_colorBindTex, nullptr);
    m_colorBindTex = VK_NULL_HANDLE;
    vkDestroyImageView(m_q->m_ctx->m_dev, m_depthBindView, nullptr);
    m_depthBindView = VK_NULL_HANDLE;
    vkDestroyImage(m_q->m_ctx->m_dev, m_depthBindTex, nullptr);
    m_depthBindTex = VK_NULL_HANDLE;
    vkFreeMemory(m_q->m_ctx->m_dev, m_gpuMem, nullptr);
    m_gpuMem = VK_NULL_HANDLE;
}

VulkanTextureR::~VulkanTextureR()
{
    vkDestroyFramebuffer(m_q->m_ctx->m_dev, m_framebuffer, nullptr);
    vkDestroyImageView(m_q->m_ctx->m_dev, m_colorView, nullptr);
    vkDestroyImage(m_q->m_ctx->m_dev, m_colorTex, nullptr);
    vkDestroyImageView(m_q->m_ctx->m_dev, m_depthView, nullptr);
    vkDestroyImage(m_q->m_ctx->m_dev, m_depthTex, nullptr);
    vkDestroyImageView(m_q->m_ctx->m_dev, m_colorBindView, nullptr);
    vkDestroyImage(m_q->m_ctx->m_dev, m_colorBindTex, nullptr);
    vkDestroyImageView(m_q->m_ctx->m_dev, m_depthBindView, nullptr);
    vkDestroyImage(m_q->m_ctx->m_dev, m_depthBindTex, nullptr);
    vkFreeMemory(m_q->m_ctx->m_dev, m_gpuMem, nullptr);
    if (m_q->m_boundTarget == this)
        m_q->m_boundTarget = nullptr;
}

void VulkanGraphicsBufferD::update(int b)
{
    int slot = 1 << b;
    if ((slot & m_validSlots) == 0)
    {
        void* ptr;
        ThrowIfFailed(vkMapMemory(m_q->m_ctx->m_dev, m_mem,
                                  m_bufferInfo[slot].offset, m_bufferInfo[slot].range, 0, &ptr));
        memcpy(ptr, m_cpuBuf.get(), m_cpuSz);

        /* flush to gpu */
        VkMappedMemoryRange mappedRange;
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.pNext = nullptr;
        mappedRange.memory = m_mem;
        mappedRange.offset = m_bufferInfo[slot].offset;
        mappedRange.size = m_bufferInfo[slot].range;
        ThrowIfFailed(vkFlushMappedMemoryRanges(m_q->m_ctx->m_dev, 1, &mappedRange));

        vkUnmapMemory(m_q->m_ctx->m_dev, m_mem);
        m_validSlots |= slot;
    }
}

void VulkanGraphicsBufferD::load(const void* data, size_t sz)
{
    size_t bufSz = std::min(sz, m_cpuSz);
    memcpy(m_cpuBuf.get(), data, bufSz);
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

        /* initialize texture layouts if needed */
        if (m_cpuTexLayout[b] == VK_IMAGE_LAYOUT_UNDEFINED)
        {
            SetImageLayout(cmdBuf, m_cpuTex[b], VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            SetImageLayout(cmdBuf, m_gpuTex[b], VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            m_cpuTexLayout[b] = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        }
        else
        {
            SetImageLayout(cmdBuf, m_gpuTex[b], VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }

        /* map memory */
        uint8_t* mappedData;
        ThrowIfFailed(vkMapMemory(m_q->m_ctx->m_dev, m_cpuMem, m_cpuOffsets[b], m_cpuSz, 0, reinterpret_cast<void**>(&mappedData)));

        /* copy pitch-linear data */
        const uint8_t* srcDataIt = static_cast<const uint8_t*>(m_cpuBuf.get());
        VkImageSubresource subres = {};
        subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subres.arrayLayer = 0;
        subres.mipLevel = 0;
        VkSubresourceLayout layout;
        vkGetImageSubresourceLayout(m_q->m_ctx->m_dev, m_cpuTex[b], &subres, &layout);
        uint8_t* dstDataIt = static_cast<uint8_t*>(mappedData);

        for (size_t y=0 ; y<m_height ; ++y)
        {
            memcpy(dstDataIt, srcDataIt, m_srcRowPitch);
            srcDataIt += m_srcRowPitch;
            dstDataIt += layout.rowPitch;
        }

        /* flush to gpu */
        VkMappedMemoryRange mappedRange;
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.pNext = nullptr;
        mappedRange.memory = m_cpuMem;
        mappedRange.offset = m_cpuOffsets[b];
        mappedRange.size = m_cpuSz;
        ThrowIfFailed(vkFlushMappedMemoryRanges(m_q->m_ctx->m_dev, 1, &mappedRange));
        vkUnmapMemory(m_q->m_ctx->m_dev, m_cpuMem);

        /* Put the copy command into the command buffer */
        VkImageCopy copyRegion;
        copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.srcSubresource.mipLevel = 0;
        copyRegion.srcSubresource.baseArrayLayer = 0;
        copyRegion.srcSubresource.layerCount = 1;
        copyRegion.srcOffset.x = 0;
        copyRegion.srcOffset.y = 0;
        copyRegion.srcOffset.z = 0;
        copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.dstSubresource.mipLevel = 0;
        copyRegion.dstSubresource.baseArrayLayer = 0;
        copyRegion.dstSubresource.layerCount = 1;
        copyRegion.dstOffset.x = 0;
        copyRegion.dstOffset.y = 0;
        copyRegion.dstOffset.z = 0;
        copyRegion.extent.width = m_width;
        copyRegion.extent.height = m_height;
        copyRegion.extent.depth = 1;
        vkCmdCopyImage(cmdBuf, m_cpuTex[b],
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_gpuTex[b],
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        /* Set the layout for the texture image from DESTINATION_OPTIMAL to
         * SHADER_READ_ONLY */
        SetImageLayout(cmdBuf, m_gpuTex[b], VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        m_validSlots |= slot;
    }
}
void VulkanTextureD::load(const void* data, size_t sz)
{
    size_t bufSz = std::min(sz, m_cpuSz);
    memcpy(m_cpuBuf.get(), data, bufSz);
    m_validSlots = 0;
}
void* VulkanTextureD::map(size_t sz)
{
    if (sz > m_cpuSz)
        return nullptr;
    return m_cpuBuf.get();
}
void VulkanTextureD::unmap()
{
    m_validSlots = 0;
}

void VulkanDataFactory::destroyData(IGraphicsData* d)
{
    std::unique_lock<std::mutex> lk(m_committedMutex);
    VulkanData* data = static_cast<VulkanData*>(d);
    m_committedData.erase(data);
    delete data;
}

void VulkanDataFactory::destroyAllData()
{
    std::unique_lock<std::mutex> lk(m_committedMutex);
    for (IGraphicsData* data : m_committedData)
        delete static_cast<VulkanData*>(data);
    m_committedData.clear();
}

VulkanDataFactory::VulkanDataFactory(IGraphicsContext* parent, VulkanContext* ctx, uint32_t drawSamples)
: m_parent(parent), m_ctx(ctx), m_drawSamples(drawSamples)
{
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.pNext = nullptr;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    ThrowIfFailed(vkCreateSampler(ctx->m_dev, &samplerInfo, nullptr, &ctx->m_linearSampler));

    VkDescriptorSetLayoutBinding layoutBindings[BOO_GLSL_MAX_UNIFORM_COUNT + BOO_GLSL_MAX_TEXTURE_COUNT];
    for (int i=0 ; i<BOO_GLSL_MAX_UNIFORM_COUNT ; ++i)
    {
        layoutBindings[i].binding = i;
        layoutBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        layoutBindings[i].descriptorCount = 1;
        layoutBindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
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

    ThrowIfFailed(vkCreateDescriptorSetLayout(ctx->m_dev, &descriptorLayout, nullptr,
                                              &ctx->m_descSetLayout));

    VkPipelineLayoutCreateInfo pipelineLayout = {};
    pipelineLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayout.setLayoutCount = 1;
    pipelineLayout.pSetLayouts = &ctx->m_descSetLayout;
    ThrowIfFailed(vkCreatePipelineLayout(ctx->m_dev, &pipelineLayout, nullptr, &ctx->m_pipelinelayout));

    VkAttachmentDescription attachments[2] = {};

    /* color attachment */
    attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[0].samples = VkSampleCountFlagBits(drawSamples);
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorAttachmentRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    /* depth attachment */
    attachments[1].format = VK_FORMAT_D24_UNORM_S8_UINT;
    attachments[1].samples = VkSampleCountFlagBits(drawSamples);
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
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
    ThrowIfFailed(vkCreateRenderPass(ctx->m_dev, &renderPass, nullptr, &ctx->m_pass));
}

IShaderPipeline* VulkanDataFactory::Context::newShaderPipeline
(const char* vertSource, const char* fragSource,
 std::vector<unsigned int>& vertBlobOut, std::vector<unsigned int>& fragBlobOut,
 std::vector<unsigned char>& pipelineBlob, IVertexFormat* vtxFmt,
 BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
 bool depthTest, bool depthWrite, bool backfaceCulling)
{
    if (vertBlobOut.empty() || fragBlobOut.empty())
    {
        const EShMessages messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
        //init_resources(DefaultBuiltInResource);

        glslang::TShader vs(EShLangVertex);
        vs.setStrings(&vertSource, 1);
        if (!vs.parse(&DefaultBuiltInResource, 110, false, messages))
        {
            Log.report(logvisor::Fatal, "unable to compile vertex shader\n%s", vs.getInfoLog());
            return nullptr;
        }

        glslang::TShader fs(EShLangFragment);
        fs.setStrings(&fragSource, 1);
        if (!fs.parse(&DefaultBuiltInResource, 110, false, messages))
        {
            Log.report(logvisor::Fatal, "unable to compile fragment shader\n%s", fs.getInfoLog());
            return nullptr;
        }

        glslang::TProgram prog;
        prog.addShader(&vs);
        prog.addShader(&fs);
        if (!prog.link(messages))
        {
            Log.report(logvisor::Fatal, "unable to link shader program\n%s", prog.getInfoLog());
            return nullptr;
        }
        glslang::GlslangToSpv(*prog.getIntermediate(EShLangVertex), vertBlobOut);
        spv::Disassemble(std::cerr, vertBlobOut);
        glslang::GlslangToSpv(*prog.getIntermediate(EShLangFragment), fragBlobOut);
        spv::Disassemble(std::cerr, fragBlobOut);
    }

    VkShaderModuleCreateInfo smCreateInfo = {};
    smCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smCreateInfo.pNext = nullptr;
    smCreateInfo.flags = 0;

    smCreateInfo.codeSize = vertBlobOut.size() * sizeof(unsigned int);
    smCreateInfo.pCode = vertBlobOut.data();
    VkShaderModule vertModule;
    ThrowIfFailed(vkCreateShaderModule(m_parent.m_ctx->m_dev, &smCreateInfo, nullptr, &vertModule));

    smCreateInfo.codeSize = fragBlobOut.size() * sizeof(unsigned int);
    smCreateInfo.pCode = fragBlobOut.data();
    VkShaderModule fragModule;
    ThrowIfFailed(vkCreateShaderModule(m_parent.m_ctx->m_dev, &smCreateInfo, nullptr, &fragModule));

    VkPipelineCacheCreateInfo cacheDataInfo = {};
    cacheDataInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheDataInfo.pNext = nullptr;
    cacheDataInfo.initialDataSize = pipelineBlob.size();
    if (cacheDataInfo.initialDataSize)
        cacheDataInfo.pInitialData = pipelineBlob.data();

    VkPipelineCache pipelineCache;
    ThrowIfFailed(vkCreatePipelineCache(m_parent.m_ctx->m_dev, &cacheDataInfo, nullptr, &pipelineCache));

    VulkanShaderPipeline* retval = new VulkanShaderPipeline(m_parent.m_ctx, vertModule, fragModule, pipelineCache,
                                                            static_cast<const VulkanVertexFormat*>(vtxFmt),
                                                            srcFac, dstFac, prim, depthTest, depthWrite, backfaceCulling);

    if (pipelineBlob.empty())
    {
        size_t cacheSz = 0;
        ThrowIfFailed(vkGetPipelineCacheData(m_parent.m_ctx->m_dev, pipelineCache, &cacheSz, nullptr));
        if (cacheSz)
        {
            pipelineBlob.resize(cacheSz);
            ThrowIfFailed(vkGetPipelineCacheData(m_parent.m_ctx->m_dev, pipelineCache, &cacheSz, pipelineBlob.data()));
            pipelineBlob.resize(cacheSz);
        }
    }

    vkDestroyPipelineCache(m_parent.m_ctx->m_dev, pipelineCache, nullptr);
    vkDestroyShaderModule(m_parent.m_ctx->m_dev, fragModule, nullptr);
    vkDestroyShaderModule(m_parent.m_ctx->m_dev, vertModule, nullptr);

    static_cast<VulkanData*>(m_deferredData.get())->m_SPs.emplace_back(retval);
    return retval;
}

IGraphicsBufferS* VulkanDataFactory::Context::newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count)
{
    VulkanGraphicsBufferS* retval = new VulkanGraphicsBufferS(use, m_parent.m_ctx, data, stride, count);
    static_cast<VulkanData*>(m_deferredData.get())->m_SBufs.emplace_back(retval);
    return retval;
}

IGraphicsBufferD* VulkanDataFactory::Context::newDynamicBuffer(BufferUse use, size_t stride, size_t count)
{
    VulkanCommandQueue* q = static_cast<VulkanCommandQueue*>(m_parent.m_parent->getCommandQueue());
    VulkanGraphicsBufferD* retval = new VulkanGraphicsBufferD(q, use, m_parent.m_ctx, stride, count);
    static_cast<VulkanData*>(m_deferredData.get())->m_DBufs.emplace_back(retval);
    return retval;
}

ITextureS* VulkanDataFactory::Context::newStaticTexture(size_t width, size_t height, size_t mips,
                                                        TextureFormat fmt, const void* data, size_t sz)
{
    VulkanTextureS* retval = new VulkanTextureS(m_parent.m_ctx, width, height, mips, fmt, data, sz);
    static_cast<VulkanData*>(m_deferredData.get())->m_STexs.emplace_back(retval);
    return retval;
}

ITextureSA* VulkanDataFactory::Context::newStaticArrayTexture(size_t width, size_t height, size_t layers,
                                                              TextureFormat fmt, const void* data, size_t sz)
{
    VulkanTextureSA* retval = new VulkanTextureSA(m_parent.m_ctx, width, height, layers, fmt, data, sz);
    static_cast<VulkanData*>(m_deferredData.get())->m_SATexs.emplace_back(retval);
    return retval;
}

ITextureD* VulkanDataFactory::Context::newDynamicTexture(size_t width, size_t height, TextureFormat fmt)
{
    VulkanCommandQueue* q = static_cast<VulkanCommandQueue*>(m_parent.m_parent->getCommandQueue());
    VulkanTextureD* retval = new VulkanTextureD(q, m_parent.m_ctx, width, height, fmt);
    static_cast<VulkanData*>(m_deferredData.get())->m_DTexs.emplace_back(retval);
    return retval;
}

ITextureR* VulkanDataFactory::Context::newRenderTexture(size_t width, size_t height,
                                                        bool enableShaderColorBinding, bool enableShaderDepthBinding)
{
    VulkanCommandQueue* q = static_cast<VulkanCommandQueue*>(m_parent.m_parent->getCommandQueue());
    VulkanTextureR* retval = new VulkanTextureR(m_parent.m_ctx, q, width, height, m_parent.m_drawSamples,
                                                enableShaderColorBinding, enableShaderDepthBinding);
    static_cast<VulkanData*>(m_deferredData.get())->m_RTexs.emplace_back(retval);
    return retval;
}

IVertexFormat* VulkanDataFactory::Context::newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements)
{
    VulkanVertexFormat* retval = new struct VulkanVertexFormat(elementCount, elements);
    static_cast<VulkanData*>(m_deferredData.get())->m_VFmts.emplace_back(retval);
    return retval;
}

IShaderDataBinding* VulkanDataFactory::Context::newShaderDataBinding(IShaderPipeline* pipeline,
        IVertexFormat* /*vtxFormat*/,
        IGraphicsBuffer* vbuf, IGraphicsBuffer* instVbuf, IGraphicsBuffer* ibuf,
        size_t ubufCount, IGraphicsBuffer** ubufs, const PipelineStage* /*ubufStages*/,
        const size_t* ubufOffs, const size_t* ubufSizes,
        size_t texCount, ITexture** texs)
{
    VulkanShaderDataBinding* retval =
        new VulkanShaderDataBinding(m_parent.m_ctx, pipeline, vbuf, instVbuf, ibuf,
                                    ubufCount, ubufs, ubufOffs, ubufSizes, texCount, texs);
    static_cast<VulkanData*>(m_deferredData.get())->m_SBinds.emplace_back(retval);
    return retval;
}

GraphicsDataToken VulkanDataFactory::commitTransaction
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
        ThrowIfFalse(MemoryTypeFromProperties(m_ctx, bufMemTypeBits, 0, &memAlloc.memoryTypeIndex));
        ThrowIfFailed(vkAllocateMemory(m_ctx->m_dev, &memAlloc, nullptr, &retval->m_bufMem));

        /* place resources */
        uint8_t* mappedData;
        ThrowIfFailed(vkMapMemory(m_ctx->m_dev, retval->m_bufMem, 0, bufMemSize, 0, reinterpret_cast<void**>(&mappedData)));

        for (std::unique_ptr<VulkanGraphicsBufferS>& buf : retval->m_SBufs)
            buf->placeForGPU(m_ctx, retval->m_bufMem, mappedData);

        /* flush static buffers to gpu */
        VkMappedMemoryRange mappedRange;
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.pNext = nullptr;
        mappedRange.memory = retval->m_bufMem;
        mappedRange.offset = 0;
        mappedRange.size = bufMemSize;
        ThrowIfFailed(vkFlushMappedMemoryRanges(m_ctx->m_dev, 1, &mappedRange));
        vkUnmapMemory(m_ctx->m_dev, retval->m_bufMem);

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
        ThrowIfFailed(vkAllocateMemory(m_ctx->m_dev, &memAlloc, nullptr, &retval->m_texMem));

        for (std::unique_ptr<VulkanTextureS>& tex : retval->m_STexs)
            tex->placeForGPU(m_ctx, retval->m_texMem);

        for (std::unique_ptr<VulkanTextureSA>& tex : retval->m_SATexs)
            tex->placeForGPU(m_ctx, retval->m_texMem);

        for (std::unique_ptr<VulkanTextureD>& tex : retval->m_DTexs)
            tex->placeForGPU(m_ctx, retval->m_texMem);
    }

    /* Execute static uploads */
    ThrowIfFailed(vkEndCommandBuffer(m_ctx->m_loadCmdBuf));
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_ctx->m_loadCmdBuf;
    ThrowIfFailed(vkQueueSubmit(m_ctx->m_queue, 1, &submitInfo, m_ctx->m_loadFence));

    /* Commit data bindings (create descriptor heaps) */
    for (std::unique_ptr<VulkanShaderDataBinding>& bind : retval->m_SBinds)
        bind->commit(m_ctx);

    /* Block handle return until data is ready on GPU */
    ThrowIfFailed(vkWaitForFences(m_ctx->m_dev, 1, &m_ctx->m_loadFence, VK_TRUE, -1));

    /* Reset fence and command buffer */
    ThrowIfFailed(vkResetFences(m_ctx->m_dev, 1, &m_ctx->m_loadFence));
    ThrowIfFailed(vkResetCommandBuffer(m_ctx->m_loadCmdBuf, 0));
    VkCommandBufferBeginInfo cmdBufBeginInfo = {};
    cmdBufBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ThrowIfFailed(vkBeginCommandBuffer(m_ctx->m_loadCmdBuf, &cmdBufBeginInfo));

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

ThreadLocalPtr<struct VulkanData> VulkanDataFactory::m_deferredData;

void VulkanCommandQueue::execute()
{
    if (!m_running)
        return;

    /* Stage dynamic uploads */
    VulkanDataFactory* gfxF = static_cast<VulkanDataFactory*>(m_parent->getDataFactory());
    std::unique_lock<std::mutex> datalk(gfxF->m_committedMutex);
    for (VulkanData* d : gfxF->m_committedData)
    {
        for (std::unique_ptr<VulkanGraphicsBufferD>& b : d->m_DBufs)
            b->update(m_fillBuf);
        for (std::unique_ptr<VulkanTextureD>& t : d->m_DTexs)
            t->update(m_fillBuf);
    }
    datalk.unlock();

    /* Perform dynamic uploads */
    if (!m_dynamicNeedsReset)
    {
        vkEndCommandBuffer(m_dynamicCmdBufs[m_fillBuf]);

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
        ThrowIfFailed(vkQueueSubmit(m_ctx->m_queue, 1, &submitInfo, m_dynamicBufFence));
    }

    /* Check on fence */
    if (vkGetFenceStatus(m_ctx->m_dev, m_drawCompleteFence) == VK_NOT_READY)
    {
        /* Abandon this list (renderer too slow) */
        resetCommandBuffer();
        m_dynamicNeedsReset = true;
        m_doPresent = false;
        return;
    }

    /* Perform texture resizes */
    if (m_texResizes.size())
    {
        for (const auto& resize : m_texResizes)
            resize.first->resize(m_ctx, resize.second.first, resize.second.second);
        m_texResizes.clear();
        resetCommandBuffer();
        m_dynamicNeedsReset = true;
        m_doPresent = false;
        return;
    }

    vkCmdEndRenderPass(m_cmdBufs[m_fillBuf]);

    m_drawBuf = m_fillBuf;
    m_fillBuf ^= 1;

    /* Queue the command buffer for execution */
    VkPipelineStageFlags pipeStageFlags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkSubmitInfo submitInfo = {};
    submitInfo.pNext = nullptr;
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &m_swapChainReadySem;
    submitInfo.pWaitDstStageMask = &pipeStageFlags;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_cmdBufs[m_drawBuf];
    submitInfo.signalSemaphoreCount = 0;
    submitInfo.pSignalSemaphores = nullptr;
    if (m_doPresent)
    {
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &m_drawCompleteSem;
    }
    ThrowIfFailed(vkQueueSubmit(m_ctx->m_queue, 1, &submitInfo, m_drawCompleteFence));

    if (m_doPresent)
    {
        VkPresentInfoKHR present;
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.pNext = nullptr;
        present.swapchainCount = 1;
        present.pSwapchains = &m_windowCtx->m_swapChain;
        present.pImageIndices = &m_windowCtx->m_backBuf;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &m_drawCompleteSem;
        present.pResults = nullptr;

        ThrowIfFailed(vkQueuePresentKHR(m_ctx->m_queue, &present));
        m_doPresent = false;
    }

    resetCommandBuffer();
    resetDynamicCommandBuffer();
}

IGraphicsCommandQueue* _NewVulkanCommandQueue(VulkanContext* ctx, VulkanContext::Window* windowCtx,
                                              IGraphicsContext* parent)
{
    return new struct VulkanCommandQueue(ctx, windowCtx, parent);
}


}
