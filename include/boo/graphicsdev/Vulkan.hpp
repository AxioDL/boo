#ifndef GDEV_VULKAN_HPP
#define GDEV_VULKAN_HPP
#if BOO_HAS_VULKAN

#include "IGraphicsDataFactory.hpp"
#include "IGraphicsCommandQueue.hpp"
#include "boo/IGraphicsContext.hpp"
#include "GLSLMacros.hpp"
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <queue>
#include "boo/graphicsdev/VulkanDispatchTable.hpp"

/* Forward-declare handle type for Vulkan Memory Allocator */
struct VmaAllocator_T;

namespace boo
{
struct BaseGraphicsData;

struct VulkanContext
{
    struct LayerProperties
    {
        VkLayerProperties properties;
        std::vector<VkExtensionProperties> extensions;
    };

    std::vector<LayerProperties> m_instanceLayerProperties;
    std::vector<const char*> m_layerNames;
    std::vector<const char*> m_instanceExtensionNames;
    VkInstance m_instance = VK_NULL_HANDLE;
    std::vector<const char*> m_deviceExtensionNames;
    std::vector<VkPhysicalDevice> m_gpus;
    VkPhysicalDeviceFeatures m_features;
    VkPhysicalDeviceProperties m_gpuProps;
    VkPhysicalDeviceMemoryProperties m_memoryProperties;
    VkDevice m_dev = VK_NULL_HANDLE;
    VmaAllocator_T* m_allocator = VK_NULL_HANDLE;
    uint32_t m_queueCount;
    uint32_t m_graphicsQueueFamilyIndex = UINT32_MAX;
    std::vector<VkQueueFamilyProperties> m_queueProps;
    VkQueue m_queue = VK_NULL_HANDLE;
    std::mutex m_queueLock;
    VkDescriptorSetLayout m_descSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelinelayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descPool = VK_NULL_HANDLE;
    VkRenderPass m_pass = VK_NULL_HANDLE;
    VkRenderPass m_passColorOnly = VK_NULL_HANDLE;
    VkCommandPool m_loadPool = VK_NULL_HANDLE;
    VkCommandBuffer m_loadCmdBuf = VK_NULL_HANDLE;
    VkFormat m_displayFormat;
    VkFormat m_internalFormat;

    struct Window
    {
        struct SwapChain
        {
            VkFormat m_format = VK_FORMAT_UNDEFINED;
            VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
            struct Buffer
            {
                VkImage m_image = VK_NULL_HANDLE;
                VkImageView m_colorView = VK_NULL_HANDLE;
                VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
                VkRenderPassBeginInfo m_passBeginInfo = {};
                void setImage(VulkanContext* ctx, VkImage image, uint32_t width, uint32_t height);
                void destroy(VkDevice dev);
            };
            std::vector<Buffer> m_bufs;
            uint32_t m_backBuf = 0;
            void destroy(VkDevice dev)
            {
                for (Buffer& buf : m_bufs)
                    buf.destroy(dev);
                m_bufs.clear();
                if (m_swapChain)
                {
                    vk::DestroySwapchainKHR(dev, m_swapChain, nullptr);
                    m_swapChain = VK_NULL_HANDLE;
                }
                m_backBuf = 0;
            }
        } m_swapChains[2];
        uint32_t m_activeSwapChain = 0;

#if _WIN32
        HWND m_hwnd = 0;
        bool m_fs = false;
        LONG m_fsStyle;
        LONG m_fsExStyle;
        RECT m_fsRect;
        int m_fsCountDown = 0;
#endif
    };
    std::unordered_map<const boo::IWindow*, std::unique_ptr<Window>> m_windows;

    VkSampleCountFlags m_sampleCountColor = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlags m_sampleCountDepth = VK_SAMPLE_COUNT_1_BIT;
    float m_anisotropy = 1.f;
    bool m_deepColor = false;

    std::unordered_map<uint32_t, VkSampler> m_samplers;

    bool initVulkan(std::string_view appName, PFN_vkGetInstanceProcAddr getVkProc);
    bool enumerateDevices();
    void initDevice();
    void destroyDevice();
    void initSwapChain(Window& windowCtx, VkSurfaceKHR surface, VkFormat format, VkColorSpaceKHR colorspace);

    struct SwapChainResize
    {
        Window& m_windowCtx;
        VkSurfaceKHR m_surface;
        VkFormat m_format;
        VkColorSpaceKHR m_colorspace;
        SWindowRect m_rect;
        SwapChainResize(Window& windowCtx, VkSurfaceKHR surface,
                        VkFormat format, VkColorSpaceKHR colorspace,
                        const SWindowRect& rect)
        : m_windowCtx(windowCtx), m_surface(surface),
          m_format(format), m_colorspace(colorspace), m_rect(rect) {}
    };
    std::queue<SwapChainResize> m_deferredResizes;
    std::mutex m_resizeLock;
    void resizeSwapChain(Window& windowCtx, VkSurfaceKHR surface,
                         VkFormat format, VkColorSpaceKHR colorspace,
                         const SWindowRect& rect);
    bool _resizeSwapChains();
};
extern VulkanContext g_VulkanContext;

class VulkanDataFactory : public IGraphicsDataFactory
{
public:
    class Context : public IGraphicsDataFactory::Context
    {
        friend class VulkanDataFactoryImpl;
        VulkanDataFactory& m_parent;
        boo::ObjToken<BaseGraphicsData> m_data;
        Context(VulkanDataFactory& parent __BooTraceArgs);
        ~Context();
    public:
        Platform platform() const {return Platform::Vulkan;}
        const SystemChar* platformName() const {return _S("Vulkan");}

        boo::ObjToken<IGraphicsBufferS> newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count);
        boo::ObjToken<IGraphicsBufferD> newDynamicBuffer(BufferUse use, size_t stride, size_t count);

        boo::ObjToken<ITextureS> newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                                  TextureClampMode clampMode, const void* data, size_t sz);
        boo::ObjToken<ITextureSA> newStaticArrayTexture(size_t width, size_t height, size_t layers, size_t mips,
                                                        TextureFormat fmt, TextureClampMode clampMode,
                                                        const void* data, size_t sz);
        boo::ObjToken<ITextureD> newDynamicTexture(size_t width, size_t height, TextureFormat fmt, TextureClampMode clampMode);
        boo::ObjToken<ITextureR> newRenderTexture(size_t width, size_t height, TextureClampMode clampMode,
                                                  size_t colorBindCount, size_t depthBindCount);

        bool bindingNeedsVertexFormat() const {return false;}
        boo::ObjToken<IVertexFormat> newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements,
                                                     size_t baseVert = 0, size_t baseInst = 0);

        boo::ObjToken<IShaderPipeline> newShaderPipeline(const char* vertSource, const char* fragSource,
                                                         std::vector<unsigned int>* vertBlobOut,
                                                         std::vector<unsigned int>* fragBlobOut,
                                                         std::vector<unsigned char>* pipelineBlob,
                                                         const boo::ObjToken<IVertexFormat>& vtxFmt,
                                                         BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                                                         ZTest depthTest, bool depthWrite, bool colorWrite,
                                                         bool alphaWrite, CullMode culling, bool overwriteAlpha = true);

        boo::ObjToken<IShaderPipeline> newShaderPipeline(const char* vertSource, const char* fragSource,
                                                         const boo::ObjToken<IVertexFormat>& vtxFmt,
                                                         BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                                                         ZTest depthTest, bool depthWrite, bool colorWrite,
                                                         bool alphaWrite, CullMode culling)
        {
            return newShaderPipeline(vertSource, fragSource, nullptr, nullptr, nullptr,
                                     vtxFmt, srcFac, dstFac, prim, depthTest, depthWrite,
                                     colorWrite, alphaWrite, culling);
        }

        boo::ObjToken<IShaderDataBinding>
        newShaderDataBinding(const boo::ObjToken<IShaderPipeline>& pipeline,
                             const boo::ObjToken<IVertexFormat>& vtxFormat,
                             const boo::ObjToken<IGraphicsBuffer>& vbo,
                             const boo::ObjToken<IGraphicsBuffer>& instVbo,
                             const boo::ObjToken<IGraphicsBuffer>& ibo,
                             size_t ubufCount, const boo::ObjToken<IGraphicsBuffer>* ubufs, const PipelineStage* ubufStages,
                             const size_t* ubufOffs, const size_t* ubufSizes,
                             size_t texCount, const boo::ObjToken<ITexture>* texs,
                             const int* bindIdxs, const bool* bindDepth,
                             size_t baseVert = 0, size_t baseInst = 0);
    };
};

}

#endif
#endif // GDEV_VULKAN_HPP
