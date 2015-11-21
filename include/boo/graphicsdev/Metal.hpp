#ifndef GDEV_METAL_HPP
#define GDEV_METAL_HPP
#ifdef __APPLE__

#include <Availability.h>

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 101100
#define BOO_HAS_METAL 1

#include "IGraphicsDataFactory.hpp"
#include "IGraphicsCommandQueue.hpp"
#include "boo/IGraphicsContext.hpp"
#include <vector>
#include <unordered_set>
#include <unordered_map>

namespace boo
{
struct MetalContext;
    
class MetalDataFactory : public IGraphicsDataFactory
{
    IGraphicsContext* m_parent;
    IGraphicsData* m_deferredData = nullptr;
    std::unordered_set<IGraphicsData*> m_committedData;
    struct MetalContext* m_ctx;
public:
    MetalDataFactory(IGraphicsContext* parent, MetalContext* ctx);
    ~MetalDataFactory() {}
    
    Platform platform() const {return Platform::Metal;}
    const char* platformName() const {return "Metal";}
    
    IGraphicsBufferS* newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count);
    IGraphicsBufferS* newStaticBuffer(BufferUse use, std::unique_ptr<uint8_t[]>&& data, size_t stride, size_t count);
    IGraphicsBufferD* newDynamicBuffer(BufferUse use, size_t stride, size_t count);
    
    ITextureS* newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                const void* data, size_t sz);
    ITextureS* newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                std::unique_ptr<uint8_t[]>&& data, size_t sz);
    ITextureD* newDynamicTexture(size_t width, size_t height, TextureFormat fmt);
    ITextureR* newRenderTexture(size_t width, size_t height, size_t samples);
    
    bool bindingNeedsVertexFormat() const {return false;}
    IVertexFormat* newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements);
    
    IShaderPipeline* newShaderPipeline(const char* vertSource, const char* fragSource,
                                       IVertexFormat* vtxFmt, ITextureR* target,
                                       BlendFactor srcFac, BlendFactor dstFac,
                                       bool depthTest, bool depthWrite, bool backfaceCulling);
    
    IShaderDataBinding*
    newShaderDataBinding(IShaderPipeline* pipeline,
                         IVertexFormat* vtxFormat,
                         IGraphicsBuffer* vbo, IGraphicsBuffer* ibo,
                         size_t ubufCount, IGraphicsBuffer** ubufs,
                         size_t texCount, ITexture** texs);
    
    void reset();
    IGraphicsData* commit();
    void destroyData(IGraphicsData*);
    void destroyAllData();
};

}

#else
#define BOO_HAS_METAL 0
#endif

#endif // __APPLE__
#endif // GDEV_METAL_HPP
