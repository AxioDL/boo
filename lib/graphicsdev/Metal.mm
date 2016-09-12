#include "../mac/CocoaCommon.hpp"
#if BOO_HAS_METAL
#include "logvisor/logvisor.hpp"
#include "boo/graphicsdev/Metal.hpp"
#include "boo/IGraphicsContext.hpp"
#include <vector>

#if !__has_feature(objc_arc)
#error ARC Required
#endif

#define MAX_UNIFORM_COUNT 8
#define MAX_TEXTURE_COUNT 8

namespace boo
{
static logvisor::Module Log("boo::Metal");
struct MetalCommandQueue;

ThreadLocalPtr<struct MetalData> MetalDataFactory::m_deferredData;
struct MetalData : IGraphicsData
{
    std::vector<std::unique_ptr<class MetalShaderPipeline>> m_SPs;
    std::vector<std::unique_ptr<struct MetalShaderDataBinding>> m_SBinds;
    std::vector<std::unique_ptr<class MetalGraphicsBufferS>> m_SBufs;
    std::vector<std::unique_ptr<class MetalGraphicsBufferD>> m_DBufs;
    std::vector<std::unique_ptr<class MetalTextureS>> m_STexs;
    std::vector<std::unique_ptr<class MetalTextureSA>> m_SATexs;
    std::vector<std::unique_ptr<class MetalTextureD>> m_DTexs;
    std::vector<std::unique_ptr<class MetalTextureR>> m_RTexs;
    std::vector<std::unique_ptr<struct MetalVertexFormat>> m_VFmts;
};

#define MTL_STATIC MTLResourceCPUCacheModeWriteCombined|MTLResourceStorageModeShared
#define MTL_DYNAMIC MTLResourceCPUCacheModeWriteCombined|MTLResourceStorageModeShared

class MetalGraphicsBufferS : public IGraphicsBufferS
{
    friend class MetalDataFactory;
    friend struct MetalCommandQueue;
    MetalGraphicsBufferS(BufferUse use, MetalContext* ctx, const void* data, size_t stride, size_t count)
    : m_stride(stride), m_count(count), m_sz(stride * count)
    {
        m_buf = [ctx->m_dev newBufferWithBytes:data length:m_sz options:MTL_STATIC];
    }
public:
    size_t m_stride;
    size_t m_count;
    size_t m_sz;
    id<MTLBuffer> m_buf;
    ~MetalGraphicsBufferS() = default;
};

class MetalGraphicsBufferD : public IGraphicsBufferD
{
    friend class MetalDataFactory;
    friend struct MetalCommandQueue;
    MetalCommandQueue* m_q;
    std::unique_ptr<uint8_t[]> m_cpuBuf;
    int m_validSlots = 0;
    MetalGraphicsBufferD(MetalCommandQueue* q, BufferUse use, MetalContext* ctx, size_t stride, size_t count)
    : m_q(q), m_stride(stride), m_count(count), m_sz(stride * count)
    {
        m_cpuBuf.reset(new uint8_t[m_sz]);
        m_bufs[0] = [ctx->m_dev newBufferWithLength:m_sz options:MTL_DYNAMIC];
        m_bufs[1] = [ctx->m_dev newBufferWithLength:m_sz options:MTL_DYNAMIC];
    }
    void update(int b);
public:
    size_t m_stride;
    size_t m_count;
    size_t m_sz;
    id<MTLBuffer> m_bufs[2];
    MetalGraphicsBufferD() = default;

    void load(const void* data, size_t sz);
    void* map(size_t sz);
    void unmap();
};

class MetalTextureS : public ITextureS
{
    friend class MetalDataFactory;
    MetalTextureS(MetalContext* ctx, size_t width, size_t height, size_t mips,
                  TextureFormat fmt, const void* data, size_t sz)
    {
        MTLPixelFormat pfmt = MTLPixelFormatRGBA8Unorm;
        NSUInteger ppitchNum = 4;
        NSUInteger ppitchDenom = 1;
        switch (fmt)
        {
        case TextureFormat::I8:
            pfmt = MTLPixelFormatR8Unorm;
            ppitchNum = 1;
            break;
        case TextureFormat::DXT1:
            pfmt = MTLPixelFormatBC1_RGBA;
            ppitchNum = 1;
            ppitchDenom = 2;
        default: break;
        }

        @autoreleasepool
        {
            MTLTextureDescriptor* desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pfmt
                                                               width:width height:height
                                                           mipmapped:(mips>1)?YES:NO];
            desc.usage = MTLTextureUsageShaderRead;
            desc.mipmapLevelCount = mips;
            m_tex = [ctx->m_dev newTextureWithDescriptor:desc];
            const uint8_t* dataIt = reinterpret_cast<const uint8_t*>(data);
            for (size_t i=0 ; i<mips ; ++i)
            {
                [m_tex replaceRegion:MTLRegionMake2D(0, 0, width, height)
                         mipmapLevel:i
                           withBytes:dataIt
                         bytesPerRow:width * ppitchNum / ppitchDenom];
                dataIt += width * height * ppitchNum / ppitchDenom;
                width /= 2;
                height /= 2;
            }
        }
    }
public:
    id<MTLTexture> m_tex;
    ~MetalTextureS() = default;
};

class MetalTextureSA : public ITextureSA
{
    friend class MetalDataFactory;
    MetalTextureSA(MetalContext* ctx, size_t width, size_t height, size_t layers,
                   TextureFormat fmt, const void* data, size_t sz)
    {
        MTLPixelFormat pfmt = MTLPixelFormatRGBA8Unorm;
        NSUInteger ppitch = 4;
        switch (fmt)
        {
        case TextureFormat::I8:
            pfmt = MTLPixelFormatR8Unorm;
            ppitch = 1;
            break;
        default: break;
        }

        @autoreleasepool
        {
            MTLTextureDescriptor* desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pfmt
                                                               width:width height:height
                                                           mipmapped:NO];
            desc.textureType = MTLTextureType2DArray;
            desc.arrayLength = layers;
            desc.usage = MTLTextureUsageShaderRead;
            m_tex = [ctx->m_dev newTextureWithDescriptor:desc];
            const uint8_t* dataIt = reinterpret_cast<const uint8_t*>(data);
            for (size_t i=0 ; i<layers ; ++i)
            {
                [m_tex replaceRegion:MTLRegionMake2D(0, 0, width, height)
                         mipmapLevel:0
                               slice:i
                           withBytes:dataIt
                         bytesPerRow:width * ppitch
                       bytesPerImage:width * height * ppitch];
                dataIt += width * height * ppitch;
            }
        }
    }
public:
    id<MTLTexture> m_tex;
    ~MetalTextureSA() = default;
};

class MetalTextureD : public ITextureD
{
    friend class MetalDataFactory;
    friend struct MetalCommandQueue;
    MetalCommandQueue* m_q;
    size_t m_width = 0;
    size_t m_height = 0;
    std::unique_ptr<uint8_t[]> m_cpuBuf;
    size_t m_cpuSz;
    size_t m_pxPitch;
    int m_validSlots = 0;
    MetalTextureD(MetalCommandQueue* q, MetalContext* ctx, size_t width, size_t height, TextureFormat fmt)
    : m_q(q), m_width(width), m_height(height)
    {
        MTLPixelFormat format;
        switch (fmt)
        {
        case TextureFormat::RGBA8:
            format = MTLPixelFormatRGBA8Unorm;
            m_pxPitch = 4;
            break;
        case TextureFormat::I8:
            format = MTLPixelFormatR8Unorm;
            m_pxPitch = 1;
            break;
        default:
            Log.report(logvisor::Fatal, "unsupported tex format");
        }

        m_cpuSz = width * height * m_pxPitch;
        m_cpuBuf.reset(new uint8_t[m_cpuSz]);

        @autoreleasepool
        {
            MTLTextureDescriptor* desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                               width:width height:height
                                                           mipmapped:NO];
            desc.usage = MTLTextureUsageShaderRead;
            m_texs[0] = [ctx->m_dev newTextureWithDescriptor:desc];
            m_texs[1] = [ctx->m_dev newTextureWithDescriptor:desc];
        }
    }
    void update(int b);
public:
    id<MTLTexture> m_texs[2];
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
    bool m_enableShaderBindTexture;

    void Setup(MetalContext* ctx, size_t width, size_t height, size_t samples,
               bool enableShaderBindTexture)
    {
        m_width = width;
        m_height = height;

        @autoreleasepool
        {
            MTLTextureDescriptor* desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:width height:height
                                                           mipmapped:NO];
            desc.storageMode = MTLStorageModePrivate;

            if (samples > 1)
            {
                desc.textureType = MTLTextureType2DMultisample;
                desc.sampleCount = samples;
                desc.usage = MTLTextureUsageRenderTarget;
                m_colorTex = [ctx->m_dev newTextureWithDescriptor:desc];

                if (enableShaderBindTexture)
                {
                    desc.usage = MTLTextureUsageShaderRead;
                    m_colorBindTex = [ctx->m_dev newTextureWithDescriptor:desc];
                }

                desc.usage = MTLTextureUsageRenderTarget;
                desc.pixelFormat = MTLPixelFormatDepth32Float;
                m_depthTex = [ctx->m_dev newTextureWithDescriptor:desc];
            }
            else
            {
                desc.textureType = MTLTextureType2D;
                desc.sampleCount = 1;
                desc.usage = MTLTextureUsageRenderTarget;
                m_colorTex = [ctx->m_dev newTextureWithDescriptor:desc];

                if (enableShaderBindTexture)
                {
                    desc.usage = MTLTextureUsageShaderRead;
                    m_colorBindTex = [ctx->m_dev newTextureWithDescriptor:desc];
                }

                desc.usage = MTLTextureUsageRenderTarget;
                desc.pixelFormat = MTLPixelFormatDepth32Float;
                m_depthTex = [ctx->m_dev newTextureWithDescriptor:desc];
            }

            {
                m_passDesc = [MTLRenderPassDescriptor renderPassDescriptor];

                m_passDesc.colorAttachments[0].texture = m_colorTex;
                m_passDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;
                m_passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

                m_passDesc.depthAttachment.texture = m_depthTex;
                m_passDesc.depthAttachment.loadAction = MTLLoadActionLoad;
                m_passDesc.depthAttachment.storeAction = MTLStoreActionStore;
                m_passDesc.depthAttachment.clearDepth = 0.f;
            }

            {
                m_clearDepthPassDesc = [MTLRenderPassDescriptor renderPassDescriptor];

                m_clearDepthPassDesc.colorAttachments[0].texture = m_colorTex;
                m_clearDepthPassDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;
                m_clearDepthPassDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

                m_clearDepthPassDesc.depthAttachment.texture = m_depthTex;
                m_clearDepthPassDesc.depthAttachment.loadAction = MTLLoadActionClear;
                m_clearDepthPassDesc.depthAttachment.storeAction = MTLStoreActionStore;
                m_clearDepthPassDesc.depthAttachment.clearDepth = 0.f;
            }

            {
                m_clearColorPassDesc = [MTLRenderPassDescriptor renderPassDescriptor];

                m_clearColorPassDesc.colorAttachments[0].texture = m_colorTex;
                m_clearColorPassDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
                m_clearColorPassDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

                m_clearColorPassDesc.depthAttachment.texture = m_depthTex;
                m_clearColorPassDesc.depthAttachment.loadAction = MTLLoadActionLoad;
                m_clearColorPassDesc.depthAttachment.storeAction = MTLStoreActionStore;
                m_clearColorPassDesc.depthAttachment.clearDepth = 0.f;
            }

            {
                m_clearBothPassDesc = [MTLRenderPassDescriptor renderPassDescriptor];

                m_clearBothPassDesc.colorAttachments[0].texture = m_colorTex;
                m_clearBothPassDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
                m_clearBothPassDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

                m_clearBothPassDesc.depthAttachment.texture = m_depthTex;
                m_clearBothPassDesc.depthAttachment.loadAction = MTLLoadActionClear;
                m_clearBothPassDesc.depthAttachment.storeAction = MTLStoreActionStore;
                m_clearBothPassDesc.depthAttachment.clearDepth = 0.f;
            }
        }
    }

    MetalTextureR(MetalContext* ctx, size_t width, size_t height, size_t samples,
                  bool enableShaderBindTexture)
    : m_width(width), m_height(height), m_samples(samples), m_enableShaderBindTexture(enableShaderBindTexture)
    {
        if (samples == 0) m_samples = 1;
        Setup(ctx, width, height, samples, enableShaderBindTexture);
    }
public:
    size_t samples() const {return m_samples;}
    id<MTLTexture> m_colorTex;
    id<MTLTexture> m_depthTex;
    id<MTLTexture> m_colorBindTex;
    MTLRenderPassDescriptor* m_passDesc;
    MTLRenderPassDescriptor* m_clearDepthPassDesc;
    MTLRenderPassDescriptor* m_clearColorPassDesc;
    MTLRenderPassDescriptor* m_clearBothPassDesc;
    ~MetalTextureR() = default;

    void resize(MetalContext* ctx, size_t width, size_t height)
    {
        if (width < 1)
            width = 1;
        if (height < 1)
            height = 1;
        m_width = width;
        m_height = height;
        Setup(ctx, width, height, m_samples, m_enableShaderBindTexture);
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

static const MTLVertexFormat SEMANTIC_TYPE_TABLE[] =
{
    MTLVertexFormatInvalid,
    MTLVertexFormatFloat3,
    MTLVertexFormatFloat4,
    MTLVertexFormatFloat3,
    MTLVertexFormatFloat4,
    MTLVertexFormatFloat4,
    MTLVertexFormatUChar4Normalized,
    MTLVertexFormatFloat2,
    MTLVertexFormatFloat4,
    MTLVertexFormatFloat4,
    MTLVertexFormatFloat4
};

struct MetalVertexFormat : IVertexFormat
{
    size_t m_elementCount;
    MTLVertexDescriptor* m_vdesc;
    MetalVertexFormat(size_t elementCount, const VertexElementDescriptor* elements)
    : m_elementCount(elementCount)
    {
        size_t stride = 0;
        size_t instStride = 0;
        for (size_t i=0 ; i<elementCount ; ++i)
        {
            const VertexElementDescriptor* elemin = &elements[i];
            int semantic = int(elemin->semantic & VertexSemantic::SemanticMask);
            if ((elemin->semantic & VertexSemantic::Instanced) != VertexSemantic::None)
                instStride += SEMANTIC_SIZE_TABLE[semantic];
            else
                stride += SEMANTIC_SIZE_TABLE[semantic];
        }

        m_vdesc = [MTLVertexDescriptor vertexDescriptor];
        MTLVertexBufferLayoutDescriptor* layoutDesc = m_vdesc.layouts[0];
        layoutDesc.stride = stride;
        layoutDesc.stepFunction = MTLVertexStepFunctionPerVertex;
        layoutDesc.stepRate = 1;

        layoutDesc = m_vdesc.layouts[1];
        layoutDesc.stride = instStride;
        layoutDesc.stepFunction = MTLVertexStepFunctionPerInstance;
        layoutDesc.stepRate = 1;

        size_t offset = 0;
        size_t instOffset = 0;
        for (size_t i=0 ; i<elementCount ; ++i)
        {
            const VertexElementDescriptor* elemin = &elements[i];
            MTLVertexAttributeDescriptor* attrDesc = m_vdesc.attributes[i];
            int semantic = int(elemin->semantic & VertexSemantic::SemanticMask);
            if ((elemin->semantic & VertexSemantic::Instanced) != VertexSemantic::None)
            {
                attrDesc.offset = instOffset;
                attrDesc.bufferIndex = 1;
                instOffset += SEMANTIC_SIZE_TABLE[semantic];
            }
            else
            {
                attrDesc.offset = offset;
                attrDesc.bufferIndex = 0;
                offset += SEMANTIC_SIZE_TABLE[semantic];
            }
            attrDesc.format = SEMANTIC_TYPE_TABLE[semantic];
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

static const MTLPrimitiveType PRIMITIVE_TABLE[] =
{
    MTLPrimitiveTypeTriangle,
    MTLPrimitiveTypeTriangleStrip
};

class MetalShaderPipeline : public IShaderPipeline
{
    friend class MetalDataFactory;
    friend class MetalCommandQueue;
    MTLCullMode m_cullMode = MTLCullModeNone;
    MTLPrimitiveType m_drawPrim;

    MetalShaderPipeline(MetalContext* ctx, id<MTLFunction> vert, id<MTLFunction> frag,
                        const MetalVertexFormat* vtxFmt, NSUInteger targetSamples,
                        BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                        bool depthTest, bool depthWrite, bool backfaceCulling)
    : m_drawPrim(PRIMITIVE_TABLE[int(prim)])
    {
        if (backfaceCulling)
            m_cullMode = MTLCullModeBack;

        MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
        desc.vertexFunction = vert;
        desc.fragmentFunction = frag;
        desc.vertexDescriptor = vtxFmt->m_vdesc;
        desc.sampleCount = targetSamples;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[0].blendingEnabled = dstFac != BlendFactor::Zero;
        desc.colorAttachments[0].sourceRGBBlendFactor = BLEND_FACTOR_TABLE[int(srcFac)];
        desc.colorAttachments[0].destinationRGBBlendFactor = BLEND_FACTOR_TABLE[int(dstFac)];
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        desc.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
        NSError* err = nullptr;
        m_state = [ctx->m_dev newRenderPipelineStateWithDescriptor:desc error:&err];
        if (err)
            Log.report(logvisor::Fatal, "error making shader pipeline: %s",
                       [[err localizedDescription] UTF8String]);

        MTLDepthStencilDescriptor* dsDesc = [MTLDepthStencilDescriptor new];
        if (depthTest)
            dsDesc.depthCompareFunction = MTLCompareFunctionGreaterEqual;
        dsDesc.depthWriteEnabled = depthWrite;
        m_dsState = [ctx->m_dev newDepthStencilStateWithDescriptor:dsDesc];
    }
public:
    id<MTLRenderPipelineState> m_state;
    id<MTLDepthStencilState> m_dsState;
    ~MetalShaderPipeline() = default;
    MetalShaderPipeline& operator=(const MetalShaderPipeline&) = delete;
    MetalShaderPipeline(const MetalShaderPipeline&) = delete;

    void bind(id<MTLRenderCommandEncoder> enc)
    {
        [enc setRenderPipelineState:m_state];
        [enc setDepthStencilState:m_dsState];
        [enc setCullMode:m_cullMode];
    }
};

static id<MTLBuffer> GetBufferGPUResource(const IGraphicsBuffer* buf, int idx)
{
    if (buf->dynamic())
    {
        const MetalGraphicsBufferD* cbuf = static_cast<const MetalGraphicsBufferD*>(buf);
        return cbuf->m_bufs[idx];
    }
    else
    {
        const MetalGraphicsBufferS* cbuf = static_cast<const MetalGraphicsBufferS*>(buf);
        return cbuf->m_buf;
    }
}

static id<MTLTexture> GetTextureGPUResource(const ITexture* tex, int idx)
{
    switch (tex->type())
    {
    case TextureType::Dynamic:
    {
        const MetalTextureD* ctex = static_cast<const MetalTextureD*>(tex);
        return ctex->m_texs[idx];
    }
    case TextureType::Static:
    {
        const MetalTextureS* ctex = static_cast<const MetalTextureS*>(tex);
        return ctex->m_tex;
    }
    case TextureType::StaticArray:
    {
        const MetalTextureSA* ctex = static_cast<const MetalTextureSA*>(tex);
        return ctex->m_tex;
    }
    case TextureType::Render:
    {
        const MetalTextureR* ctex = static_cast<const MetalTextureR*>(tex);
        return ctex->m_colorBindTex;
    }
    default: break;
    }
    return nullptr;
}

struct MetalShaderDataBinding : IShaderDataBinding
{
    MetalShaderPipeline* m_pipeline;
    IGraphicsBuffer* m_vbuf;
    IGraphicsBuffer* m_instVbo;
    IGraphicsBuffer* m_ibuf;
    size_t m_ubufCount;
    std::unique_ptr<IGraphicsBuffer*[]> m_ubufs;
    std::unique_ptr<size_t[]> m_ubufOffs;
    std::unique_ptr<bool[]> m_fubufs;
    size_t m_texCount;
    std::unique_ptr<ITexture*[]> m_texs;
    MetalShaderDataBinding(MetalContext* ctx,
                           IShaderPipeline* pipeline,
                           IGraphicsBuffer* vbuf, IGraphicsBuffer* instVbo, IGraphicsBuffer* ibuf,
                           size_t ubufCount, IGraphicsBuffer** ubufs, const PipelineStage* ubufStages,
                           const size_t* ubufOffs, const size_t* ubufSizes,
                           size_t texCount, ITexture** texs)
    : m_pipeline(static_cast<MetalShaderPipeline*>(pipeline)),
    m_vbuf(vbuf),
    m_instVbo(instVbo),
    m_ibuf(ibuf),
    m_ubufCount(ubufCount),
    m_ubufs(new IGraphicsBuffer*[ubufCount]),
    m_texCount(texCount),
    m_texs(new ITexture*[texCount])
    {
        if (ubufCount && ubufStages)
        {
            m_fubufs.reset(new bool[ubufCount]);
            for (size_t i=0 ; i<ubufCount ; ++i)
                m_fubufs[i] = ubufStages[i] == PipelineStage::Fragment;
        }

        if (ubufCount && ubufOffs && ubufSizes)
        {
            m_ubufOffs.reset(new size_t[ubufCount]);
            for (size_t i=0 ; i<ubufCount ; ++i)
            {
#ifndef NDEBUG
                if (ubufOffs[i] % 256)
                    Log.report(logvisor::Fatal, "non-256-byte-aligned uniform-offset %d provided to newShaderDataBinding", int(i));
#endif
                m_ubufOffs[i] = ubufOffs[i];
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
            m_texs[i] = texs[i];
        }
    }

    void bind(id<MTLRenderCommandEncoder> enc, int b)
    {
        m_pipeline->bind(enc);
        if (m_vbuf)
            [enc setVertexBuffer:GetBufferGPUResource(m_vbuf, b) offset:0 atIndex:0];
        if (m_instVbo)
            [enc setVertexBuffer:GetBufferGPUResource(m_instVbo, b) offset:0 atIndex:1];
        if (m_ubufOffs)
            for (size_t i=0 ; i<m_ubufCount ; ++i)
            {
                if (m_fubufs && m_fubufs[i])
                    [enc setFragmentBuffer:GetBufferGPUResource(m_ubufs[i], b) offset:m_ubufOffs[i] atIndex:i+2];
                else
                    [enc setVertexBuffer:GetBufferGPUResource(m_ubufs[i], b) offset:m_ubufOffs[i] atIndex:i+2];
            }
        else
            for (size_t i=0 ; i<m_ubufCount ; ++i)
            {
                if (m_fubufs && m_fubufs[i])
                    [enc setFragmentBuffer:GetBufferGPUResource(m_ubufs[i], b) offset:0 atIndex:i+2];
                else
                    [enc setVertexBuffer:GetBufferGPUResource(m_ubufs[i], b) offset:0 atIndex:i+2];
            }
        for (size_t i=0 ; i<m_texCount ; ++i)
            if (m_texs[i])
                [enc setFragmentTexture:GetTextureGPUResource(m_texs[i], b) atIndex:i];
    }
};

struct MetalCommandQueue : IGraphicsCommandQueue
{
    Platform platform() const {return IGraphicsDataFactory::Platform::Metal;}
    const char* platformName() const {return "Metal";}
    MetalContext* m_ctx;
    IWindow* m_parentWindow;
    IGraphicsContext* m_parent;
    id<MTLCommandBuffer> m_cmdBuf;
    id<MTLRenderCommandEncoder> m_enc;
    bool m_running = true;

    size_t m_fillBuf = 0;
    size_t m_drawBuf = 0;

    MetalCommandQueue(MetalContext* ctx, IWindow* parentWindow, IGraphicsContext* parent)
    : m_ctx(ctx), m_parentWindow(parentWindow), m_parent(parent)
    {
        @autoreleasepool
        {
            m_cmdBuf = [ctx->m_q commandBuffer];
        }
    }

    void stopRenderer()
    {
        m_running = false;
        if (m_inProgress)
            [m_cmdBuf waitUntilCompleted];
    }

    ~MetalCommandQueue()
    {
        if (m_running) stopRenderer();
    }

    MetalShaderDataBinding* m_boundData = nullptr;
    MTLPrimitiveType m_currentPrimitive = MTLPrimitiveTypeTriangle;
    void setShaderDataBinding(IShaderDataBinding* binding)
    {
        MetalShaderDataBinding* cbind = static_cast<MetalShaderDataBinding*>(binding);
        cbind->bind(m_enc, m_fillBuf);
        m_boundData = cbind;
        m_currentPrimitive = cbind->m_pipeline->m_drawPrim;
    }

    MetalTextureR* m_boundTarget = nullptr;
    void _setRenderTarget(ITextureR* target, bool clearColor, bool clearDepth)
    {
        MetalTextureR* ctarget = static_cast<MetalTextureR*>(target);
        [m_enc endEncoding];
        @autoreleasepool
        {
            if (clearColor && clearDepth)
                m_enc = [m_cmdBuf renderCommandEncoderWithDescriptor:ctarget->m_clearBothPassDesc];
            else if (clearColor)
                m_enc = [m_cmdBuf renderCommandEncoderWithDescriptor:ctarget->m_clearColorPassDesc];
            else if (clearDepth)
                m_enc = [m_cmdBuf renderCommandEncoderWithDescriptor:ctarget->m_clearDepthPassDesc];
            else
                m_enc = [m_cmdBuf renderCommandEncoderWithDescriptor:ctarget->m_passDesc];
            [m_enc setFrontFacingWinding:MTLWindingCounterClockwise];
        }
        if (ctarget == m_boundTarget && (m_boundVp.width || m_boundVp.height))
            [m_enc setViewport:m_boundVp];
        m_boundTarget = ctarget;
    }

    void setRenderTarget(ITextureR* target)
    {
        _setRenderTarget(target, false, false);
    }

    MTLViewport m_boundVp = {};
    void setViewport(const SWindowRect& rect, float znear, float zfar)
    {
        m_boundVp = MTLViewport{double(rect.location[0]), double(rect.location[1]),
                                double(rect.size[0]), double(rect.size[1]), znear, zfar};
        [m_enc setViewport:m_boundVp];
    }

    MTLScissorRect m_boundScissor = {};
    void setScissor(const SWindowRect& rect)
    {
        if (m_boundTarget)
        {
            SWindowRect intersectRect = rect.intersect(SWindowRect(0, 0, m_boundTarget->m_width, m_boundTarget->m_height));
            m_boundScissor = MTLScissorRect{NSUInteger(intersectRect.location[0]),
                NSUInteger(m_boundTarget->m_height - intersectRect.location[1] - intersectRect.size[1]),
                NSUInteger(intersectRect.size[0]), NSUInteger(intersectRect.size[1])};
            [m_enc setScissorRect:m_boundScissor];
        }
    }

    std::unordered_map<MetalTextureR*, std::pair<size_t, size_t>> m_texResizes;
    void resizeRenderTexture(ITextureR* tex, size_t width, size_t height)
    {
        MetalTextureR* ctex = static_cast<MetalTextureR*>(tex);
        m_texResizes[ctex] = std::make_pair(width, height);
    }

    void schedulePostFrameHandler(std::function<void(void)>&& func)
    {
        func();
    }

    void flushBufferUpdates() {}

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
        _setRenderTarget(m_boundTarget, render, depth);
    }

    void draw(size_t start, size_t count)
    {
        [m_enc drawPrimitives:m_currentPrimitive vertexStart:start vertexCount:count];
    }

    void drawIndexed(size_t start, size_t count)
    {
        [m_enc drawIndexedPrimitives:m_currentPrimitive
                          indexCount:count
                           indexType:MTLIndexTypeUInt32
                         indexBuffer:GetBufferGPUResource(m_boundData->m_ibuf, m_fillBuf)
                   indexBufferOffset:start*4];
    }

    void drawInstances(size_t start, size_t count, size_t instCount)
    {
        [m_enc drawPrimitives:m_currentPrimitive
                  vertexStart:start vertexCount:count instanceCount:instCount];
    }

    void drawInstancesIndexed(size_t start, size_t count, size_t instCount)
    {
        [m_enc drawIndexedPrimitives:m_currentPrimitive
                          indexCount:count
                           indexType:MTLIndexTypeUInt32
                         indexBuffer:GetBufferGPUResource(m_boundData->m_ibuf, m_fillBuf)
                   indexBufferOffset:start*4
                       instanceCount:instCount];
    }

    void resolveBindTexture(ITextureR* texture, const SWindowRect& rect, bool tlOrigin, bool color, bool depth)
    {
        MetalTextureR* tex = static_cast<MetalTextureR*>(texture);
        if (color && tex->m_enableShaderBindTexture)
        {
            [m_enc endEncoding];
            @autoreleasepool
            {
                SWindowRect intersectRect = rect.intersect(SWindowRect(0, 0, tex->m_width, tex->m_height));
                NSUInteger y = tlOrigin ? intersectRect.location[1] : int(tex->m_height) - intersectRect.location[1] - intersectRect.size[1];
                MTLOrigin origin = {NSUInteger(intersectRect.location[0]), y, 0};
                id<MTLBlitCommandEncoder> blitEnc = [m_cmdBuf blitCommandEncoder];
                [blitEnc copyFromTexture:tex->m_colorTex
                             sourceSlice:0
                             sourceLevel:0
                            sourceOrigin:origin
                              sourceSize:MTLSizeMake(intersectRect.size[0], intersectRect.size[1], 1)
                               toTexture:tex->m_colorBindTex
                        destinationSlice:0
                        destinationLevel:0
                       destinationOrigin:origin];
                [blitEnc endEncoding];
                m_enc = [m_cmdBuf renderCommandEncoderWithDescriptor:tex->m_passDesc];
                [m_enc setFrontFacingWinding:MTLWindingCounterClockwise];

                if (m_boundVp.width || m_boundVp.height)
                    [m_enc setViewport:m_boundVp];
                if (m_boundScissor.width || m_boundScissor.height)
                    [m_enc setScissorRect:m_boundScissor];
            }
        }
    }

    MetalTextureR* m_needsDisplay = nullptr;
    void resolveDisplay(ITextureR* source)
    {
        m_needsDisplay = static_cast<MetalTextureR*>(source);
    }

    bool m_inProgress = false;
    void execute()
    {
        if (!m_running)
            return;

        /* Update dynamic data here */
        MetalDataFactory* gfxF = static_cast<MetalDataFactory*>(m_parent->getDataFactory());
        std::unique_lock<std::mutex> datalk(gfxF->m_committedMutex);
        for (MetalData* d : gfxF->m_committedData)
        {
            for (std::unique_ptr<MetalGraphicsBufferD>& b : d->m_DBufs)
                b->update(m_fillBuf);
            for (std::unique_ptr<MetalTextureD>& t : d->m_DTexs)
                t->update(m_fillBuf);
        }
        datalk.unlock();

        @autoreleasepool
        {
            [m_enc endEncoding];
            m_enc = nullptr;

            /* Abandon if in progress (renderer too slow) */
            if (m_inProgress)
            {
                m_cmdBuf = [m_ctx->m_q commandBuffer];
                return;
            }

            /* Perform texture resizes */
            if (m_texResizes.size())
            {
                for (const auto& resize : m_texResizes)
                    resize.first->resize(m_ctx, resize.second.first, resize.second.second);
                m_texResizes.clear();
                m_cmdBuf = [m_ctx->m_q commandBuffer];
                return;
            }

            /* Wrap up and present if needed */
            if (m_needsDisplay)
            {
                MetalContext::Window& w = m_ctx->m_windows[m_parentWindow];
                @autoreleasepool
                {
                    {
                        std::unique_lock<std::mutex> lk(w.m_resizeLock);
                        if (w.m_needsResize)
                        {
                            w.m_metalLayer.drawableSize = w.m_size;
                            w.m_needsResize = NO;
                            m_needsDisplay = nullptr;
                            return;
                        }
                    }
                    id<CAMetalDrawable> drawable = [w.m_metalLayer nextDrawable];
                    if (drawable)
                    {
                        id<MTLTexture> dest = drawable.texture;
                        if (m_needsDisplay->m_colorTex.width == dest.width &&
                            m_needsDisplay->m_colorTex.height == dest.height)
                        {
                            id<MTLBlitCommandEncoder> blitEnc = [m_cmdBuf blitCommandEncoder];
                            [blitEnc copyFromTexture:m_needsDisplay->m_colorTex
                                         sourceSlice:0
                                         sourceLevel:0
                                        sourceOrigin:MTLOriginMake(0, 0, 0)
                                          sourceSize:MTLSizeMake(dest.width, dest.height, 1)
                                           toTexture:dest
                                    destinationSlice:0
                                    destinationLevel:0
                                   destinationOrigin:MTLOriginMake(0, 0, 0)];
                            [blitEnc endEncoding];
                            [m_cmdBuf presentDrawable:drawable];
                        }
                    }
                }
                m_needsDisplay = nullptr;
            }

            m_drawBuf = m_fillBuf;
            m_fillBuf ^= 1;

            [m_cmdBuf addCompletedHandler:^(id<MTLCommandBuffer> buf) {m_inProgress = false;}];
            m_inProgress = true;
            [m_cmdBuf commit];
            m_cmdBuf = [m_ctx->m_q commandBuffer];
        }
    }
};

void MetalGraphicsBufferD::update(int b)
{
    int slot = 1 << b;
    if ((slot & m_validSlots) == 0)
    {
        id<MTLBuffer> res = m_bufs[b];
        memcpy(res.contents, m_cpuBuf.get(), m_sz);
        m_validSlots |= slot;
    }
}
void MetalGraphicsBufferD::load(const void* data, size_t sz)
{
    size_t bufSz = std::min(sz, m_sz);
    memcpy(m_cpuBuf.get(), data, bufSz);
    m_validSlots = 0;
}
void* MetalGraphicsBufferD::map(size_t sz)
{
    if (sz > m_sz)
        return nullptr;
    return m_cpuBuf.get();
}
void MetalGraphicsBufferD::unmap()
{
    m_validSlots = 0;
}

void MetalTextureD::update(int b)
{
    int slot = 1 << b;
    if ((slot & m_validSlots) == 0)
    {
        id<MTLTexture> res = m_texs[b];
        [res replaceRegion:MTLRegionMake2D(0, 0, m_width, m_height)
               mipmapLevel:0 withBytes:m_cpuBuf.get() bytesPerRow:m_width*m_pxPitch];
        m_validSlots |= slot;
    }
}
void MetalTextureD::load(const void* data, size_t sz)
{
    size_t bufSz = std::min(sz, m_cpuSz);
    memcpy(m_cpuBuf.get(), data, bufSz);
    m_validSlots = 0;
}
void* MetalTextureD::map(size_t sz)
{
    if (sz > m_cpuSz)
        return nullptr;
    return m_cpuBuf.get();
}
void MetalTextureD::unmap()
{
    m_validSlots = 0;
}

MetalDataFactory::MetalDataFactory(IGraphicsContext* parent, MetalContext* ctx, uint32_t sampleCount)
: m_parent(parent), m_ctx(ctx), m_sampleCount(sampleCount) {}

IGraphicsBufferS* MetalDataFactory::Context::newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count)
{
    MetalGraphicsBufferS* retval = new MetalGraphicsBufferS(use, m_parent.m_ctx, data, stride, count);
    m_deferredData->m_SBufs.emplace_back(retval);
    return retval;
}
IGraphicsBufferD* MetalDataFactory::Context::newDynamicBuffer(BufferUse use, size_t stride, size_t count)
{
    MetalCommandQueue* q = static_cast<MetalCommandQueue*>(m_parent.m_parent->getCommandQueue());
    MetalGraphicsBufferD* retval = new MetalGraphicsBufferD(q, use, m_parent.m_ctx, stride, count);
    m_deferredData->m_DBufs.emplace_back(retval);
    return retval;
}

ITextureS* MetalDataFactory::Context::newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                                       const void* data, size_t sz)
{
    MetalTextureS* retval = new MetalTextureS(m_parent.m_ctx, width, height, mips, fmt, data, sz);
    m_deferredData->m_STexs.emplace_back(retval);
    return retval;
}
ITextureSA* MetalDataFactory::Context::newStaticArrayTexture(size_t width, size_t height, size_t layers, TextureFormat fmt,
                                                             const void* data, size_t sz)
{
    MetalTextureSA* retval = new MetalTextureSA(m_parent.m_ctx, width, height, layers, fmt, data, sz);
    m_deferredData->m_SATexs.emplace_back(retval);
    return retval;
}
ITextureD* MetalDataFactory::Context::newDynamicTexture(size_t width, size_t height, TextureFormat fmt)
{
    MetalCommandQueue* q = static_cast<MetalCommandQueue*>(m_parent.m_parent->getCommandQueue());
    MetalTextureD* retval = new MetalTextureD(q, m_parent.m_ctx, width, height, fmt);
    m_deferredData->m_DTexs.emplace_back(retval);
    return retval;
}
ITextureR* MetalDataFactory::Context::newRenderTexture(size_t width, size_t height,
                                                       bool enableShaderColorBinding, bool enableShaderDepthBinding)
{
    MetalTextureR* retval = new MetalTextureR(m_parent.m_ctx, width, height, m_parent.m_sampleCount, enableShaderColorBinding);
    m_deferredData->m_RTexs.emplace_back(retval);
    return retval;
}

IVertexFormat* MetalDataFactory::Context::newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements)
{
    MetalVertexFormat* retval = new struct MetalVertexFormat(elementCount, elements);
    m_deferredData->m_VFmts.emplace_back(retval);
    return retval;
}

IShaderPipeline* MetalDataFactory::Context::newShaderPipeline(const char* vertSource, const char* fragSource,
                                                              IVertexFormat* vtxFmt, unsigned targetSamples,
                                                              BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                                                              bool depthTest, bool depthWrite, bool backfaceCulling)
{
    MTLCompileOptions* compOpts = [MTLCompileOptions new];
    compOpts.languageVersion = MTLLanguageVersion1_1;
    NSError* err = nullptr;

    id<MTLLibrary> vertShaderLib = [m_parent.m_ctx->m_dev newLibraryWithSource:@(vertSource)
                                                                       options:compOpts
                                                                         error:&err];
    if (!vertShaderLib)
        Log.report(logvisor::Fatal, "error compiling vert shader: %s", [[err localizedDescription] UTF8String]);
    id<MTLFunction> vertFunc = [vertShaderLib newFunctionWithName:@"vmain"];

    id<MTLLibrary> fragShaderLib = [m_parent.m_ctx->m_dev newLibraryWithSource:@(fragSource)
                                                                       options:compOpts
                                                                         error:&err];
    if (!fragShaderLib)
        Log.report(logvisor::Fatal, "error compiling frag shader: %s", [[err localizedDescription] UTF8String]);
    id<MTLFunction> fragFunc = [fragShaderLib newFunctionWithName:@"fmain"];

    MetalShaderPipeline* retval = new MetalShaderPipeline(m_parent.m_ctx, vertFunc, fragFunc,
                                                          static_cast<const MetalVertexFormat*>(vtxFmt), targetSamples,
                                                          srcFac, dstFac, prim, depthTest, depthWrite, backfaceCulling);
    m_deferredData->m_SPs.emplace_back(retval);
    return retval;
}

IShaderDataBinding*
MetalDataFactory::Context::newShaderDataBinding(IShaderPipeline* pipeline,
                                                IVertexFormat* vtxFormat,
                                                IGraphicsBuffer* vbuf, IGraphicsBuffer* instVbo, IGraphicsBuffer* ibuf,
                                                size_t ubufCount, IGraphicsBuffer** ubufs, const PipelineStage* ubufStages,
                                                const size_t* ubufOffs, const size_t* ubufSizes,
                                                size_t texCount, ITexture** texs)
{
    MetalShaderDataBinding* retval =
    new MetalShaderDataBinding(m_parent.m_ctx, pipeline, vbuf, instVbo, ibuf,
                               ubufCount, ubufs, ubufStages, ubufOffs, ubufSizes, texCount, texs);
    m_deferredData->m_SBinds.emplace_back(retval);
    return retval;
}

GraphicsDataToken MetalDataFactory::commitTransaction(const FactoryCommitFunc& trans)
{
    if (m_deferredData.get())
        Log.report(logvisor::Fatal, "nested commitTransaction usage detected");
    m_deferredData.reset(new MetalData());

    MetalDataFactory::Context ctx(*this);
    if (!trans(ctx))
    {
        delete m_deferredData.get();
        m_deferredData.reset();
        return GraphicsDataToken(this, nullptr);
    }

    std::unique_lock<std::mutex> lk(m_committedMutex);
    MetalData* retval = m_deferredData.get();
    m_deferredData.reset();
    m_committedData.insert(retval);
    return GraphicsDataToken(this, retval);
}
void MetalDataFactory::destroyData(IGraphicsData* d)
{
    std::unique_lock<std::mutex> lk(m_committedMutex);
    MetalData* data = static_cast<MetalData*>(d);
    m_committedData.erase(data);
    delete data;
}
void MetalDataFactory::destroyAllData()
{
    std::unique_lock<std::mutex> lk(m_committedMutex);
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

#endif
