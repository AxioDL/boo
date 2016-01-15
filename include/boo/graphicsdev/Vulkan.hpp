#ifndef GDEV_VULKAN_HPP
#define GDEV_VULKAN_HPP

#include "IGraphicsDataFactory.hpp"
#include "IGraphicsCommandQueue.hpp"
#include "boo/IGraphicsContext.hpp"
#include <vector>
#include <unordered_set>
#include <mutex>

namespace boo
{

class VulkanDataFactory : public IGraphicsDataFactory
{
    friend struct VulkanCommandQueue;
    IGraphicsContext* m_parent;
    static ThreadLocalPtr<struct GLData> m_deferredData;
    std::unordered_set<struct GLData*> m_committedData;
    std::mutex m_committedMutex;
    std::vector<int> m_texUnis;
    void destroyData(IGraphicsData*);
    void destroyAllData();
public:
    VulkanDataFactory(IGraphicsContext* parent);
    ~VulkanDataFactory() {destroyAllData();}

    Platform platform() const {return Platform::Vulkan;}
    const SystemChar* platformName() const {return _S("Vulkan");}

    IGraphicsBufferS* newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count);
    IGraphicsBufferS* newStaticBuffer(BufferUse use, std::unique_ptr<uint8_t[]>&& data, size_t stride, size_t count);
    IGraphicsBufferD* newDynamicBuffer(BufferUse use, size_t stride, size_t count);

    ITextureS* newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                const void* data, size_t sz);
    ITextureS* newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                std::unique_ptr<uint8_t[]>&& data, size_t sz);
    ITextureSA* newStaticArrayTexture(size_t width, size_t height, size_t layers, TextureFormat fmt,
                                      const void* data, size_t sz);
    ITextureD* newDynamicTexture(size_t width, size_t height, TextureFormat fmt);
    ITextureR* newRenderTexture(size_t width, size_t height, size_t samples);

    bool bindingNeedsVertexFormat() const {return true;}
    IVertexFormat* newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements);

    IShaderPipeline* newShaderPipeline(const char* vertSource, const char* fragSource,
                                       std::vector<unsigned int>& vertBlobOut, std::vector<unsigned int>& fragBlobOut,
                                       std::vector<unsigned int>& pipelineBlob,
                                       size_t texCount, const char* texArrayName,
                                       size_t uniformBlockCount, const char** uniformBlockNames,
                                       BlendFactor srcFac, BlendFactor dstFac,
                                       bool depthTest, bool depthWrite, bool backfaceCulling);

    IShaderPipeline* newShaderPipeline(const char* vertSource, const char* fragSource,
                                       size_t texCount, const char* texArrayName,
                                       size_t uniformBlockCount, const char** uniformBlockNames,
                                       BlendFactor srcFac, BlendFactor dstFac,
                                       bool depthTest, bool depthWrite, bool backfaceCulling)
    {
        std::vector<unsigned int> vertBlob;
        std::vector<unsigned int> fragBlob;
        std::vector<unsigned int> pipelineBlob;
        return newShaderPipeline(vertSource, fragSource, vertBlob, fragBlob, pipelineBlob,
                                 texCount, texArrayName, uniformBlockCount, uniformBlockNames,
                                 srcFac, dstFac, depthTest, depthWrite, backfaceCulling);
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

#endif // GDEV_VULKAN_HPP
