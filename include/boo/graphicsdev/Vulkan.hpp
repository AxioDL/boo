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
#include <vulkan/vulkan.h>

namespace boo
{

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
    VkPhysicalDeviceProperties m_gpuProps;
    VkPhysicalDeviceMemoryProperties m_memoryProperties;
    VkDevice m_dev;
    uint32_t m_queueCount;
    uint32_t m_graphicsQueueFamilyIndex = UINT32_MAX;
    std::vector<VkQueueFamilyProperties> m_queueProps;
    VkQueue m_queue;
    VkDescriptorSetLayout m_descSetLayout;
    VkPipelineLayout m_pipelinelayout;
    VkRenderPass m_pass;
    VkCommandPool m_loadPool;
    VkCommandBuffer m_loadCmdBuf;
    VkFence m_loadFence;
    VkSampler m_linearSampler;
    struct Window
    {
        VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
        struct Buffer
        {
            VkImage m_image;
            VkImageView m_view;
            void destroy(VkDevice dev)
            {
                vkDestroyImageView(dev, m_view, nullptr);
                vkDestroyImage(dev, m_image, nullptr);
            }
        };
        std::vector<Buffer> m_bufs;
        uint32_t m_backBuf = 0;
        size_t width, height;
    };
    std::unordered_map<const boo::IWindow*, std::unique_ptr<Window>> m_windows;

    void initVulkan(const char* appName);
    void initDevice();
    void initSwapChain(Window& windowCtx, VkSurfaceKHR surface, VkFormat format);
};
extern VulkanContext g_VulkanContext;

class VulkanDataFactory : public IGraphicsDataFactory
{
    friend struct VulkanCommandQueue;
    IGraphicsContext* m_parent;
    VulkanContext* m_ctx;
    uint32_t m_drawSamples;
    static ThreadLocalPtr<struct VulkanData> m_deferredData;
    std::unordered_set<struct VulkanData*> m_committedData;
    std::mutex m_committedMutex;
    std::vector<int> m_texUnis;
    void destroyData(IGraphicsData*);
    void destroyAllData();
public:
    VulkanDataFactory(IGraphicsContext* parent, VulkanContext* ctx, uint32_t drawSamples);
    ~VulkanDataFactory() {destroyAllData();}

    Platform platform() const {return Platform::Vulkan;}
    const SystemChar* platformName() const {return _S("Vulkan");}

    IGraphicsBufferS* newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count);
    IGraphicsBufferD* newDynamicBuffer(BufferUse use, size_t stride, size_t count);

    ITextureS* newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                const void* data, size_t sz);
    GraphicsDataToken newStaticTextureNoContext(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                                const void *data, size_t sz, ITextureS*& texOut);
    ITextureSA* newStaticArrayTexture(size_t width, size_t height, size_t layers, TextureFormat fmt,
                                      const void* data, size_t sz);
    ITextureD* newDynamicTexture(size_t width, size_t height, TextureFormat fmt);
    ITextureR* newRenderTexture(size_t width, size_t height);

    bool bindingNeedsVertexFormat() const {return true;}
    IVertexFormat* newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements);

    IShaderPipeline* newShaderPipeline(const char* vertSource, const char* fragSource,
                                       std::vector<unsigned int>& vertBlobOut, std::vector<unsigned int>& fragBlobOut,
                                       std::vector<unsigned char>& pipelineBlob, IVertexFormat* vtxFmt,
                                       BlendFactor srcFac, BlendFactor dstFac,
                                       bool depthTest, bool depthWrite, bool backfaceCulling);

    IShaderPipeline* newShaderPipeline(const char* vertSource, const char* fragSource, IVertexFormat* vtxFmt,
                                       BlendFactor srcFac, BlendFactor dstFac,
                                       bool depthTest, bool depthWrite, bool backfaceCulling)
    {
        std::vector<unsigned int> vertBlob;
        std::vector<unsigned int> fragBlob;
        std::vector<unsigned char> pipelineBlob;
        return newShaderPipeline(vertSource, fragSource, vertBlob, fragBlob, pipelineBlob,
                                 vtxFmt, srcFac, dstFac, depthTest, depthWrite, backfaceCulling);
    }

    IShaderDataBinding*
    newShaderDataBinding(IShaderPipeline* pipeline,
                         IVertexFormat* vtxFormat,
                         IGraphicsBuffer* vbo, IGraphicsBuffer* instVbo, IGraphicsBuffer* ibo,
                         size_t ubufCount, IGraphicsBuffer** ubufs,
                         size_t texCount, ITexture** texs);

    void reset();
    GraphicsDataToken commit();
};

}

#endif
#endif // GDEV_VULKAN_HPP
