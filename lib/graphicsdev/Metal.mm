#include <LogVisor/LogVisor.hpp>
#include "boo/graphicsdev/Metal.hpp"
#include "../mac/CocoaCommon.hpp"
#include "boo/IGraphicsContext.hpp"
#include <vector>

#define MAX_UNIFORM_COUNT 8
#define MAX_TEXTURE_COUNT 8

namespace boo
{
static LogVisor::LogModule Log("boo::Metal");
struct MetalCommandQueue;

struct MetalData : IGraphicsData
{
    std::vector<std::unique_ptr<class MetalShaderPipeline>> m_SPs;
    std::vector<std::unique_ptr<struct MetalShaderDataBinding>> m_SBinds;
    std::vector<std::unique_ptr<class MetalGraphicsBufferS>> m_SBufs;
    std::vector<std::unique_ptr<class MetalGraphicsBufferD>> m_DBufs;
    std::vector<std::unique_ptr<class MetalTextureS>> m_STexs;
    std::vector<std::unique_ptr<class MetalTextureD>> m_DTexs;
    std::vector<std::unique_ptr<class MetalTextureR>> m_RTexs;
    std::vector<std::unique_ptr<struct MetalVertexFormat>> m_VFmts;
};

#define MTL_STATIC MTLResourceCPUCacheModeWriteCombined|MTLResourceStorageModePrivate
#define MTL_DYNAMIC MTLResourceCPUCacheModeWriteCombined|MTLResourceStorageModeShared

class MetalGraphicsBufferS : public IGraphicsBufferS
{
    friend class MetalDataFactory;
    friend struct MetalCommandQueue;
    MetalGraphicsBufferS(BufferUse use, MetalContext* ctx, const void* data, size_t stride, size_t count)
    : m_stride(stride), m_count(count), m_sz(stride * count)
    {
        m_buf = [ctx->m_dev.get() newBufferWithBytes:data length:m_sz options:MTL_STATIC];
    }
public:
    size_t m_stride;
    size_t m_count;
    size_t m_sz;
    NSPtr<id<MTLBuffer>> m_buf;
    ~MetalGraphicsBufferS() = default;
};

class MetalGraphicsBufferD : public IGraphicsBufferD
{
    friend class MetalDataFactory;
    friend struct MetalCommandQueue;
    MetalCommandQueue* m_q;
    MetalGraphicsBufferD(MetalCommandQueue* q, BufferUse use, MetalContext* ctx, size_t stride, size_t count)
    : m_q(q), m_stride(stride), m_count(count), m_sz(stride * count)
    {
        m_bufs[0] = [ctx->m_dev.get() newBufferWithLength:m_sz options:MTL_DYNAMIC];
        m_bufs[1] = [ctx->m_dev.get() newBufferWithLength:m_sz options:MTL_DYNAMIC];
    }
public:
    size_t m_stride;
    size_t m_count;
    size_t m_sz;
    NSPtr<id<MTLBuffer>> m_bufs[2];
    MetalGraphicsBufferD() = default;
    
    void load(const void* data, size_t sz);
    
    size_t m_mappedSz;
    void* map(size_t sz);
    void unmap();
};

class MetalTextureS : public ITextureS
{
    friend class MetalDataFactory;
    MetalTextureS(MetalContext* ctx, size_t width, size_t height, size_t mips,
                  TextureFormat fmt, const void* data, size_t sz)
    {
        NSPtr<MTLTextureDescriptor*> desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:width height:height
                                                       mipmapped:(mips>1)?YES:NO];
        desc.get().usage = MTLTextureUsageShaderRead;
        desc.get().mipmapLevelCount = mips;
        m_tex = [ctx->m_dev.get() newTextureWithDescriptor:desc.get()];
        const uint8_t* dataIt = reinterpret_cast<const uint8_t*>(data);
        for (size_t i=0 ; i<mips ; ++i)
        {
            [m_tex.get() replaceRegion:MTLRegionMake2D(0, 0, width, height)
                           mipmapLevel:i
                             withBytes:dataIt
                           bytesPerRow:width * 4];
            dataIt += width * height * 4;
            width /= 2;
            height /= 2;
        }
    }
public:
    NSPtr<id<MTLTexture>> m_tex;
    ~MetalTextureS() = default;
};
    
class MetalTextureD : public ITextureD
{
    friend class MetalDataFactory;
    friend struct MetalCommandQueue;
    MetalCommandQueue* m_q;
    size_t m_width = 0;
    size_t m_height = 0;
    void* m_mappedBuf;
    MetalTextureD(MetalCommandQueue* q, MetalContext* ctx, size_t width, size_t height, TextureFormat fmt)
    : m_q(q), m_width(width), m_height(height)
    {
        NSPtr<MTLTextureDescriptor*> desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:width height:height
                                                       mipmapped:NO];
        desc.get().usage = MTLTextureUsageShaderRead;
        m_texs[0] = [ctx->m_dev.get() newTextureWithDescriptor:desc.get()];
        m_texs[1] = [ctx->m_dev.get() newTextureWithDescriptor:desc.get()];
    }
public:
    NSPtr<id<MTLTexture>> m_texs[2];
    ~MetalTextureD() = default;
    
    void load(const void* data, size_t sz);
    void* map(size_t sz);
    void unmap();
};
    
class MetalTextureR : public ITextureR
{
    friend class MetalDataFactory;
    friend struct MetalCommandQueue;
    size_t m_width = 0;
    size_t m_height = 0;
    size_t m_samples = 0;
    
    void Setup(MetalContext* ctx, size_t width, size_t height, size_t samples)
    {
        NSPtr<MTLTextureDescriptor*> desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:width height:height
                                                       mipmapped:NO];
        m_passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        desc.get().usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        desc.get().storageMode = MTLStorageModePrivate;

        m_tex = [ctx->m_dev.get() newTextureWithDescriptor:desc.get()];
        
        if (samples > 1)
        {
            desc.get().textureType = MTLTextureType2DMultisample;
            desc.get().sampleCount = samples;
            m_msaaTex = [ctx->m_dev.get() newTextureWithDescriptor:desc.get()];
            
            desc.get().pixelFormat = MTLPixelFormatDepth32Float;
            m_depthTex = [ctx->m_dev.get() newTextureWithDescriptor:desc.get()];
            
            m_passDesc.get().colorAttachments[0].texture = m_msaaTex.get();
            m_passDesc.get().colorAttachments[0].resolveTexture = m_tex.get();
            m_passDesc.get().colorAttachments[0].loadAction = MTLLoadActionClear;
            m_passDesc.get().colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
            
            m_passDesc.get().depthAttachment.texture = m_depthTex.get();
            m_passDesc.get().depthAttachment.loadAction = MTLLoadActionClear;
            m_passDesc.get().depthAttachment.storeAction = MTLStoreActionDontCare;
        }
        else
        {
            desc.get().pixelFormat = MTLPixelFormatDepth32Float;
            m_depthTex = [ctx->m_dev.get() newTextureWithDescriptor:desc.get()];
            
            m_passDesc.get().colorAttachments[0].texture = m_tex.get();
            m_passDesc.get().colorAttachments[0].loadAction = MTLLoadActionClear;
            m_passDesc.get().colorAttachments[0].storeAction = MTLStoreActionStore;
            
            m_passDesc.get().depthAttachment.texture = m_depthTex.get();
            m_passDesc.get().depthAttachment.loadAction = MTLLoadActionClear;
            m_passDesc.get().depthAttachment.storeAction = MTLStoreActionDontCare;
        }
    }
    
    MetalTextureR(MetalContext* ctx, size_t width, size_t height, size_t samples)
    : m_width(width), m_height(height), m_samples(samples)
    {
        if (samples == 0) m_samples = 1;
        Setup(ctx, width, height, samples);
    }
public:
    size_t samples() const {return m_samples;}
    NSPtr<id<MTLTexture>> m_tex;
    NSPtr<id<MTLTexture>> m_msaaTex;
    NSPtr<id<MTLTexture>> m_depthTex;
    NSPtr<MTLRenderPassDescriptor*> m_passDesc;
    ~MetalTextureR() = default;
    
    void resize(MetalContext* ctx, size_t width, size_t height)
    {
        if (width < 1)
            width = 1;
        if (height < 1)
            height = 1;
        m_width = width;
        m_height = height;
        Setup(ctx, width, height, m_samples);
    }
    
    id<MTLTexture> getRenderColorRes() {if (m_samples > 1) return m_msaaTex.get(); return m_tex.get();}
};
    
static const size_t SEMANTIC_SIZE_TABLE[] =
{
    12,
    12,
    4,
    8,
    16
};

static const MTLVertexFormat SEMANTIC_TYPE_TABLE[] =
{
    MTLVertexFormatFloat3,
    MTLVertexFormatFloat3,
    MTLVertexFormatUChar4Normalized,
    MTLVertexFormatFloat2,
    MTLVertexFormatFloat4
};

struct MetalVertexFormat : IVertexFormat
{
    size_t m_elementCount;
    NSPtr<MTLVertexDescriptor*> m_vdesc;
    MetalVertexFormat(size_t elementCount, const VertexElementDescriptor* elements)
    : m_elementCount(elementCount)
    {
        size_t stride = 0;
        for (size_t i=0 ; i<elementCount ; ++i)
        {
            const VertexElementDescriptor* elemin = &elements[i];
            stride += SEMANTIC_SIZE_TABLE[elemin->semantic];
        }
        
        m_vdesc = [MTLVertexDescriptor vertexDescriptor];
        size_t offset = 0;
        for (size_t i=0 ; i<elementCount ; ++i)
        {
            const VertexElementDescriptor* elemin = &elements[i];
            MTLVertexAttributeDescriptor* attrDesc = m_vdesc.get().attributes[i];
            MTLVertexBufferLayoutDescriptor* layoutDesc = m_vdesc.get().layouts[i];
            attrDesc.format = SEMANTIC_TYPE_TABLE[elemin->semantic];
            attrDesc.offset = offset;
            attrDesc.bufferIndex = 0;
            layoutDesc.stride = stride;
            layoutDesc.stepFunction = MTLVertexStepFunctionPerVertex;
            layoutDesc.stepRate = 1;
            offset += SEMANTIC_SIZE_TABLE[elemin->semantic];
        }
    }
};
    
static const MTLBlendFactor BLEND_FACTOR_TABLE[] =
{
    MTLBlendFactorZero,
    MTLBlendFactorOne,
    MTLBlendFactorSourceColor,
    MTLBlendFactorOneMinusSourceColor,
    MTLBlendFactorDestinationColor,
    MTLBlendFactorOneMinusDestinationColor,
    MTLBlendFactorSourceAlpha,
    MTLBlendFactorOneMinusSourceAlpha,
    MTLBlendFactorDestinationAlpha,
    MTLBlendFactorOneMinusDestinationAlpha
};

class MetalShaderPipeline : public IShaderPipeline
{
    friend class MetalDataFactory;
    MTLCullMode m_cullMode = MTLCullModeNone;
    
    MetalShaderPipeline(MetalContext* ctx, id<MTLFunction> vert, id<MTLFunction> frag,
                        const MetalVertexFormat* vtxFmt, MetalTextureR* target,
                        BlendFactor srcFac, BlendFactor dstFac,
                        bool depthTest, bool depthWrite, bool backfaceCulling)
    {
        if (backfaceCulling)
            m_cullMode = MTLCullModeBack;
        
        NSPtr<MTLRenderPipelineDescriptor*> desc = [MTLRenderPipelineDescriptor new];
        desc.get().vertexFunction = vert;
        desc.get().fragmentFunction = frag;
        desc.get().vertexDescriptor = vtxFmt->m_vdesc.get();
        desc.get().sampleCount = target->samples();
        desc.get().colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
        desc.get().colorAttachments[0].blendingEnabled = dstFac != BlendFactorZero;
        desc.get().colorAttachments[0].sourceRGBBlendFactor = BLEND_FACTOR_TABLE[srcFac];
        desc.get().colorAttachments[0].destinationRGBBlendFactor = BLEND_FACTOR_TABLE[dstFac];
        desc.get().depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        desc.get().inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
        NSError* err = nullptr;
        m_state = [ctx->m_dev.get() newRenderPipelineStateWithDescriptor:desc.get() error:&err];
        if (err)
            Log.report(LogVisor::FatalError, "error making shader pipeline: %s",
                       [[err localizedDescription] UTF8String]);
        
        NSPtr<MTLDepthStencilDescriptor*> dsDesc = [MTLDepthStencilDescriptor new];
        if (depthTest)
            dsDesc.get().depthCompareFunction = MTLCompareFunctionLessEqual;
        dsDesc.get().depthWriteEnabled = depthWrite;
        m_dsState = [ctx->m_dev.get() newDepthStencilStateWithDescriptor:dsDesc.get()];
    }
public:
    NSPtr<id<MTLRenderPipelineState>> m_state;
    NSPtr<id<MTLDepthStencilState>> m_dsState;
    ~MetalShaderPipeline() = default;
    MetalShaderPipeline& operator=(const MetalShaderPipeline&) = delete;
    MetalShaderPipeline(const MetalShaderPipeline&) = delete;
    
    void bind(id<MTLRenderCommandEncoder> enc)
    {
        [enc setRenderPipelineState:m_state.get()];
        [enc setDepthStencilState:m_dsState.get()];
        [enc setCullMode:m_cullMode];
    }
};
    
static id<MTLBuffer> GetBufferGPUResource(const IGraphicsBuffer* buf, int idx)
{
    if (buf->dynamic())
    {
        const MetalGraphicsBufferD* cbuf = static_cast<const MetalGraphicsBufferD*>(buf);
        return cbuf->m_bufs[idx].get();
    }
    else
    {
        const MetalGraphicsBufferS* cbuf = static_cast<const MetalGraphicsBufferS*>(buf);
        return cbuf->m_buf.get();
    }
}

static id<MTLTexture> GetTextureGPUResource(const ITexture* tex, int idx)
{
    if (tex->type() == ITexture::TextureDynamic)
    {
        const MetalTextureD* ctex = static_cast<const MetalTextureD*>(tex);
        return ctex->m_texs[idx].get();
    }
    else if (tex->type() == ITexture::TextureStatic)
    {
        const MetalTextureS* ctex = static_cast<const MetalTextureS*>(tex);
        return ctex->m_tex.get();
    }
    else if (tex->type() == ITexture::TextureRender)
    {
        const MetalTextureR* ctex = static_cast<const MetalTextureR*>(tex);
        return ctex->m_tex.get();
    }
    return nullptr;
}

struct MetalShaderDataBinding : IShaderDataBinding
{
    MetalShaderPipeline* m_pipeline;
    IGraphicsBuffer* m_vbuf;
    IGraphicsBuffer* m_ibuf;
    size_t m_ubufCount;
    std::unique_ptr<IGraphicsBuffer*[]> m_ubufs;
    size_t m_texCount;
    std::unique_ptr<ITexture*[]> m_texs;
    MetalShaderDataBinding(MetalContext* ctx,
                           IShaderPipeline* pipeline,
                           IGraphicsBuffer* vbuf, IGraphicsBuffer* ibuf,
                           size_t ubufCount, IGraphicsBuffer** ubufs,
                           size_t texCount, ITexture** texs)
    : m_pipeline(static_cast<MetalShaderPipeline*>(pipeline)),
    m_vbuf(vbuf),
    m_ibuf(ibuf),
    m_ubufCount(ubufCount),
    m_ubufs(new IGraphicsBuffer*[ubufCount]),
    m_texCount(texCount),
    m_texs(new ITexture*[texCount])
    {
        for (size_t i=0 ; i<ubufCount ; ++i)
            m_ubufs[i] = ubufs[i];
        for (size_t i=0 ; i<texCount ; ++i)
            m_texs[i] = texs[i];
    }
    
    void bind(id<MTLRenderCommandEncoder> enc, int b)
    {
        m_pipeline->bind(enc);
        [enc setVertexBuffer:GetBufferGPUResource(m_vbuf, b) offset:0 atIndex:0];
        for (size_t i=0 ; i<m_ubufCount ; ++i)
            [enc setVertexBuffer:GetBufferGPUResource(m_ubufs[i], b) offset:0 atIndex:i+1];
        for (size_t i=0 ; i<m_texCount ; ++i)
            [enc setFragmentTexture:GetTextureGPUResource(m_texs[i], b) atIndex:i];
    }
};

struct MetalCommandQueue : IGraphicsCommandQueue
{
    Platform platform() const {return IGraphicsDataFactory::PlatformMetal;}
    const char* platformName() const {return "Metal";}
    MetalContext* m_ctx;
    IWindow* m_parentWindow;
    IGraphicsContext* m_parent;
    NSPtr<id<MTLCommandBuffer>> m_cmdBuf;
    NSPtr<id<MTLRenderCommandEncoder>> m_enc;
    
    size_t m_fillBuf = 0;
    size_t m_drawBuf = 0;
    
    MetalCommandQueue(MetalContext* ctx, IWindow* parentWindow, IGraphicsContext* parent)
    : m_ctx(ctx), m_parentWindow(parentWindow), m_parent(parent)
    {
        m_cmdBuf = [ctx->m_q.get() commandBuffer];
    }
    
    MetalShaderDataBinding* m_boundData = nullptr;
    void setShaderDataBinding(IShaderDataBinding* binding)
    {
        MetalShaderDataBinding* cbind = static_cast<MetalShaderDataBinding*>(binding);
        cbind->bind(m_enc.get(), m_fillBuf);
        m_boundData = cbind;
    }
    
    MetalTextureR* m_boundTarget = nullptr;
    void setRenderTarget(ITextureR* target)
    {
        MetalTextureR* ctarget = static_cast<MetalTextureR*>(target);
        [m_enc.get() endEncoding];
        m_enc = [m_cmdBuf.get() renderCommandEncoderWithDescriptor:ctarget->m_passDesc.get()];
        m_boundTarget = ctarget;
    }
    
    void setViewport(const SWindowRect& rect)
    {
        MTLViewport vp = {double(rect.location[0]), double(rect.location[1]),
                          double(rect.size[0]), double(rect.size[1]), 0.0, 1.0};
        [m_enc.get() setViewport:vp];
    }
    
    std::unordered_map<MetalTextureR*, std::pair<size_t, size_t>> m_texResizes;
    void resizeRenderTexture(ITextureR* tex, size_t width, size_t height)
    {
        MetalTextureR* ctex = static_cast<MetalTextureR*>(tex);
        m_texResizes[ctex] = std::make_pair(width, height);
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
    
    MTLPrimitiveType m_primType = MTLPrimitiveTypeTriangle;
    void setDrawPrimitive(Primitive prim)
    {
        if (prim == PrimitiveTriangles)
            m_primType = MTLPrimitiveTypeTriangle;
        else if (prim == PrimitiveTriStrips)
            m_primType = MTLPrimitiveTypeTriangleStrip;
    }
    
    void draw(size_t start, size_t count)
    {
        [m_enc.get() drawPrimitives:m_primType vertexStart:start vertexCount:count];
    }
    
    void drawIndexed(size_t start, size_t count)
    {
        [m_enc.get() drawIndexedPrimitives:m_primType
                                indexCount:count
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:GetBufferGPUResource(m_boundData->m_ibuf, m_fillBuf)
                         indexBufferOffset:start*4];
    }
    
    void drawInstances(size_t start, size_t count, size_t instCount)
    {
        [m_enc.get() drawPrimitives:m_primType vertexStart:start vertexCount:count instanceCount:instCount];
    }
    
    void drawInstancesIndexed(size_t start, size_t count, size_t instCount)
    {
        [m_enc.get() drawIndexedPrimitives:m_primType
                                indexCount:count
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:GetBufferGPUResource(m_boundData->m_ibuf, m_fillBuf)
                         indexBufferOffset:start*4
                             instanceCount:instCount];
    }
    
    void resolveDisplay(ITextureR* source)
    {
        MetalContext::Window& w = m_ctx->m_windows[m_parentWindow];
        
        MetalTextureR* csource = static_cast<MetalTextureR*>(source);
        [m_enc.get() endEncoding];
        NSPtr<id<CAMetalDrawable>> drawable = [w.m_metalLayer nextDrawable];
        NSPtr<id<MTLBlitCommandEncoder>> blitEnc = [m_cmdBuf.get() blitCommandEncoder];
        [blitEnc.get() copyFromTexture:csource->m_tex.get()
                           sourceSlice:0
                           sourceLevel:0
                          sourceOrigin:MTLOriginMake(0, 0, 0)
                            sourceSize:MTLSizeMake(csource->m_width, csource->m_height, 1)
                             toTexture:drawable.get().texture
                      destinationSlice:0
                      destinationLevel:0
                     destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blitEnc.get() endEncoding];
        [m_cmdBuf.get() presentDrawable:drawable.get()];
    }
    
    bool m_inProgress = false;
    void execute()
    {
        /* Abandon if in progress (renderer too slow) */
        if (m_inProgress)
        {
            m_cmdBuf = [m_ctx->m_q.get() commandBufferWithUnretainedReferences];
            return;
        }
        
        /* Perform texture resizes */
        if (m_texResizes.size())
        {
            for (const auto& resize : m_texResizes)
                resize.first->resize(m_ctx, resize.second.first, resize.second.second);
            m_texResizes.clear();
            m_cmdBuf = [m_ctx->m_q.get() commandBufferWithUnretainedReferences];
            return;
        }
        
        m_drawBuf = m_fillBuf;
        ++m_fillBuf;
        if (m_fillBuf == 2)
            m_fillBuf = 0;
        
        [m_cmdBuf.get() addCompletedHandler:^(id<MTLCommandBuffer> buf) {m_inProgress = false;}];
        m_inProgress = true;
        [m_cmdBuf.get() commit];
        m_cmdBuf = [m_ctx->m_q.get() commandBufferWithUnretainedReferences];
    }
};
    
void MetalGraphicsBufferD::load(const void* data, size_t sz)
{
    id<MTLBuffer> res = m_bufs[m_q->m_fillBuf].get();
    memcpy(res.contents, data, sz);
    [res didModifyRange:NSMakeRange(0, sz)];
}
void* MetalGraphicsBufferD::map(size_t sz)
{
    m_mappedSz = sz;
    id<MTLBuffer> res = m_bufs[m_q->m_fillBuf].get();
    return res.contents;
}
void MetalGraphicsBufferD::unmap()
{
    id<MTLBuffer> res = m_bufs[m_q->m_fillBuf].get();
    [res didModifyRange:NSMakeRange(0, m_mappedSz)];
}

void MetalTextureD::load(const void* data, size_t sz)
{
    id<MTLTexture> res = m_texs[m_q->m_fillBuf].get();
    [res replaceRegion:MTLRegionMake2D(0, 0, m_width, m_height)
           mipmapLevel:0 withBytes:data bytesPerRow:m_width*4];
}
void* MetalTextureD::map(size_t sz)
{
    m_mappedBuf = malloc(sz);
    return m_mappedBuf;
}
void MetalTextureD::unmap()
{
    id<MTLTexture> res = m_texs[m_q->m_fillBuf].get();
    [res replaceRegion:MTLRegionMake2D(0, 0, m_width, m_height)
           mipmapLevel:0 withBytes:m_mappedBuf bytesPerRow:m_width*4];
    free(m_mappedBuf);
}
    
MetalDataFactory::MetalDataFactory(IGraphicsContext* parent, MetalContext* ctx)
: m_parent(parent), m_ctx(ctx) {}
    
IGraphicsBufferS* MetalDataFactory::newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count)
{
    MetalGraphicsBufferS* retval = new MetalGraphicsBufferS(use, m_ctx, data, stride, count);
    static_cast<MetalData*>(m_deferredData)->m_SBufs.emplace_back(retval);
    return retval;
}
IGraphicsBufferS* MetalDataFactory::newStaticBuffer(BufferUse use, std::unique_ptr<uint8_t[]>&& data, size_t stride, size_t count)
{
    std::unique_ptr<uint8_t[]> d = std::move(data);
    MetalGraphicsBufferS* retval = new MetalGraphicsBufferS(use, m_ctx, d.get(), stride, count);
    static_cast<MetalData*>(m_deferredData)->m_SBufs.emplace_back(retval);
    return retval;
}
IGraphicsBufferD* MetalDataFactory::newDynamicBuffer(BufferUse use, size_t stride, size_t count)
{
    MetalCommandQueue* q = static_cast<MetalCommandQueue*>(m_parent->getCommandQueue());
    MetalGraphicsBufferD* retval = new MetalGraphicsBufferD(q, use, m_ctx, stride, count);
    static_cast<MetalData*>(m_deferredData)->m_DBufs.emplace_back(retval);
    return retval;
}

ITextureS* MetalDataFactory::newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                              const void* data, size_t sz)
{
    MetalTextureS* retval = new MetalTextureS(m_ctx, width, height, mips, fmt, data, sz);
    static_cast<MetalData*>(m_deferredData)->m_STexs.emplace_back(retval);
    return retval;
}
ITextureS* MetalDataFactory::newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                              std::unique_ptr<uint8_t[]>&& data, size_t sz)
{
    std::unique_ptr<uint8_t[]> d = std::move(data);
    MetalTextureS* retval = new MetalTextureS(m_ctx, width, height, mips, fmt, d.get(), sz);
    static_cast<MetalData*>(m_deferredData)->m_STexs.emplace_back(retval);
    return retval;
}
ITextureD* MetalDataFactory::newDynamicTexture(size_t width, size_t height, TextureFormat fmt)
{
    MetalCommandQueue* q = static_cast<MetalCommandQueue*>(m_parent->getCommandQueue());
    MetalTextureD* retval = new MetalTextureD(q, m_ctx, width, height, fmt);
    static_cast<MetalData*>(m_deferredData)->m_DTexs.emplace_back(retval);
    return retval;
}
ITextureR* MetalDataFactory::newRenderTexture(size_t width, size_t height, size_t samples)
{
    MetalTextureR* retval = new MetalTextureR(m_ctx, width, height, samples);
    static_cast<MetalData*>(m_deferredData)->m_RTexs.emplace_back(retval);
    return retval;
}

IVertexFormat* MetalDataFactory::newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements)
{
    MetalVertexFormat* retval = new struct MetalVertexFormat(elementCount, elements);
    static_cast<MetalData*>(m_deferredData)->m_VFmts.emplace_back(retval);
    return retval;
}

IShaderPipeline* MetalDataFactory::newShaderPipeline(const char* vertSource, const char* fragSource,
                                                     IVertexFormat* vtxFmt, ITextureR* target,
                                                     BlendFactor srcFac, BlendFactor dstFac,
                                                     bool depthTest, bool depthWrite, bool backfaceCulling)
{
    NSPtr<MTLCompileOptions*> compOpts = [MTLCompileOptions new];
    compOpts.get().languageVersion = MTLLanguageVersion1_1;
    NSError* err = nullptr;
    
    NSPtr<id<MTLLibrary>> vertShaderLib = [m_ctx->m_dev.get() newLibraryWithSource:@(vertSource)
                                                                           options:compOpts.get()
                                                                             error:&err];
    if (err)
        Log.report(LogVisor::FatalError, "error compiling vert shader: %s", [[err localizedDescription] UTF8String]);
    NSPtr<id<MTLFunction>> vertFunc = [vertShaderLib.get() newFunctionWithName:@"main"];
    
    NSPtr<id<MTLLibrary>> fragShaderLib = [m_ctx->m_dev.get() newLibraryWithSource:@(fragSource)
                                                                           options:compOpts.get()
                                                                             error:&err];
    if (err)
        Log.report(LogVisor::FatalError, "error compiling frag shader: %s", [[err localizedDescription] UTF8String]);
    NSPtr<id<MTLFunction>> fragFunc = [fragShaderLib.get() newFunctionWithName:@"main"];
    
    MetalShaderPipeline* retval = new MetalShaderPipeline(m_ctx, vertFunc.get(), fragFunc.get(),
                                                          static_cast<const MetalVertexFormat*>(vtxFmt),
                                                          static_cast<MetalTextureR*>(target),
                                                          srcFac, dstFac, depthTest, depthWrite, backfaceCulling);
    static_cast<MetalData*>(m_deferredData)->m_SPs.emplace_back(retval);
    return retval;
}

IShaderDataBinding*
MetalDataFactory::newShaderDataBinding(IShaderPipeline* pipeline,
                                       IVertexFormat* vtxFormat,
                                       IGraphicsBuffer* vbuf, IGraphicsBuffer* ibuf,
                                       size_t ubufCount, IGraphicsBuffer** ubufs,
                                       size_t texCount, ITexture** texs)
{
    MetalShaderDataBinding* retval =
    new MetalShaderDataBinding(m_ctx, pipeline, vbuf, ibuf, ubufCount, ubufs, texCount, texs);
    static_cast<MetalData*>(m_deferredData)->m_SBinds.emplace_back(retval);
    return retval;
}

void MetalDataFactory::reset()
{
    delete static_cast<MetalData*>(m_deferredData);
    m_deferredData = new struct MetalData();
}
IGraphicsData* MetalDataFactory::commit()
{
    MetalData* retval = static_cast<MetalData*>(m_deferredData);
    m_deferredData = new struct MetalData();
    m_committedData.insert(retval);
    return retval;
}
void MetalDataFactory::destroyData(IGraphicsData* d)
{
    MetalData* data = static_cast<MetalData*>(d);
    m_committedData.erase(data);
    delete data;
}
void MetalDataFactory::destroyAllData()
{
    for (IGraphicsData* data : m_committedData)
        delete static_cast<MetalData*>(data);
    m_committedData.clear();
}
    
IGraphicsCommandQueue* _NewMetalCommandQueue(MetalContext* ctx, IWindow* parentWindow,
                                             IGraphicsContext* parent)
{
    return new struct MetalCommandQueue(ctx, parentWindow, parent);
}
    
}
