#ifndef GDEV_METAL_HPP
#define GDEV_METAL_HPP
#ifdef __APPLE__
#if BOO_HAS_METAL

#include "IGraphicsDataFactory.hpp"
#include "IGraphicsCommandQueue.hpp"
#include "boo/IGraphicsContext.hpp"
#include <vector>
#include <mutex>
#include <unordered_set>
#include <unordered_map>

namespace boo
{
struct MetalContext;

class MetalDataFactory : public IGraphicsDataFactory
{
    friend struct MetalCommandQueue;
    IGraphicsContext* m_parent;
    static ThreadLocalPtr<struct MetalData> m_deferredData;
    std::unordered_set<struct MetalData*> m_committedData;
    std::unordered_set<struct MetalPool*> m_committedPools;
    std::mutex m_committedMutex;
    struct MetalContext* m_ctx;
    uint32_t m_sampleCount;

    void destroyData(IGraphicsData*);
    void destroyAllData();
    void destroyPool(IGraphicsBufferPool*);
    IGraphicsBufferD* newPoolBuffer(IGraphicsBufferPool* pool, BufferUse use,
                                    size_t stride, size_t count);
public:
    MetalDataFactory(IGraphicsContext* parent, MetalContext* ctx, uint32_t sampleCount);
    ~MetalDataFactory() {}

    Platform platform() const {return Platform::Metal;}
    const char* platformName() const {return "Metal";}

    class Context : public IGraphicsDataFactory::Context
    {
        friend class MetalDataFactory;
        MetalDataFactory& m_parent;
        Context(MetalDataFactory& parent) : m_parent(parent) {}
    public:
        Platform platform() const {return Platform::Metal;}
        const char* platformName() const {return "Metal";}

        IGraphicsBufferS* newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count);
        IGraphicsBufferD* newDynamicBuffer(BufferUse use, size_t stride, size_t count);

        ITextureS* newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                    const void* data, size_t sz);
        ITextureSA* newStaticArrayTexture(size_t width, size_t height, size_t layers, TextureFormat fmt,
                                          const void* data, size_t sz);
        ITextureD* newDynamicTexture(size_t width, size_t height, TextureFormat fmt);
        ITextureR* newRenderTexture(size_t width, size_t height,
                                    bool enableShaderColorBinding, bool enableShaderDepthBinding);

        bool bindingNeedsVertexFormat() const {return false;}
        IVertexFormat* newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements,
                                       size_t baseVert = 0, size_t baseInst = 0);

        IShaderPipeline* newShaderPipeline(const char* vertSource, const char* fragSource,
                                           IVertexFormat* vtxFmt, unsigned targetSamples,
                                           BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                                           bool depthTest, bool depthWrite, bool backfaceCulling);

        IShaderDataBinding*
        newShaderDataBinding(IShaderPipeline* pipeline,
                             IVertexFormat* vtxFormat,
                             IGraphicsBuffer* vbo, IGraphicsBuffer* instVbo, IGraphicsBuffer* ibo,
                             size_t ubufCount, IGraphicsBuffer** ubufs, const PipelineStage* ubufStages,
                             const size_t* ubufOffs, const size_t* ubufSizes,
                             size_t texCount, ITexture** texs, size_t baseVert = 0, size_t baseInst = 0);
    };

    GraphicsDataToken commitTransaction(const std::function<bool(IGraphicsDataFactory::Context& ctx)>&);
    GraphicsBufferPoolToken newBufferPool();
};

}

#endif
#endif // __APPLE__
#endif // GDEV_METAL_HPP
