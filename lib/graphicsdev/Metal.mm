#include "../mac/CocoaCommon.hpp"
#if BOO_HAS_METAL
#include "logvisor/logvisor.hpp"
#include "boo/graphicsdev/Metal.hpp"
#include "boo/IGraphicsContext.hpp"
#include "Common.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "xxhash.h"

#if !__has_feature(objc_arc)
#error ARC Required
#endif

#define MAX_UNIFORM_COUNT 8
#define MAX_TEXTURE_COUNT 8

namespace boo
{
static logvisor::Module Log("boo::Metal");
struct MetalCommandQueue;
class MetalDataFactoryImpl;

struct MetalShareableShader : IShareableShader<MetalDataFactoryImpl, MetalShareableShader>
{
    id<MTLFunction> m_shader;
    MetalShareableShader(MetalDataFactoryImpl& fac, uint64_t srcKey, id<MTLFunction> s)
    : IShareableShader(fac, srcKey, 0), m_shader(s) {}
};

class MetalDataFactoryImpl : public MetalDataFactory
{
    friend struct MetalCommandQueue;
    friend class MetalDataFactory::Context;
    IGraphicsContext* m_parent;
    static ThreadLocalPtr<struct MetalData> m_deferredData;
    std::unordered_set<struct MetalData*> m_committedData;
    std::unordered_set<struct MetalPool*> m_committedPools;
    std::mutex m_committedMutex;
    std::unordered_map<uint64_t, std::unique_ptr<MetalShareableShader>> m_sharedShaders;
    struct MetalContext* m_ctx;
    uint32_t m_sampleCount;

    void destroyData(IGraphicsData*);
    void destroyAllData();
    void destroyPool(IGraphicsBufferPool*);
    IGraphicsBufferD* newPoolBuffer(IGraphicsBufferPool* pool, BufferUse use,
                                    size_t stride, size_t count);
    void deletePoolBuffer(IGraphicsBufferPool* p, IGraphicsBufferD* buf);
public:
    MetalDataFactoryImpl(IGraphicsContext* parent, MetalContext* ctx, uint32_t sampleCount);
    ~MetalDataFactoryImpl() {}

    Platform platform() const {return Platform::Metal;}
    const char* platformName() const {return "Metal";}

    GraphicsDataToken commitTransaction(const std::function<bool(IGraphicsDataFactory::Context& ctx)>&);
    GraphicsBufferPoolToken newBufferPool();

    void _unregisterShareableShader(uint64_t srcKey, uint64_t binKey) { m_sharedShaders.erase(srcKey); }
};

ThreadLocalPtr<struct MetalData> MetalDataFactoryImpl::m_deferredData;
struct MetalData : IGraphicsDataPriv
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

struct MetalPoolItem : IGraphicsDataPriv
{
    std::unique_ptr<class MetalGraphicsBufferD> m_buf;
};

struct MetalPool : IGraphicsBufferPool
{
    std::unordered_set<MetalPoolItem*> m_items;
    ~MetalPool()
    {
        for (auto& item : m_items)
            item->decrement();
    }
};

#define MTL_STATIC MTLResourceCPUCacheModeWriteCombined|MTLResourceStorageModeShared
#define MTL_DYNAMIC MTLResourceCPUCacheModeWriteCombined|MTLResourceStorageModeShared

class MetalGraphicsBufferS : public IGraphicsBufferS
{
    friend class MetalDataFactory;
    friend struct MetalCommandQueue;
    MetalGraphicsBufferS(IGraphicsData* parent, BufferUse use, MetalContext* ctx,
                         const void* data, size_t stride, size_t count)
    : boo::IGraphicsBufferS(parent), m_stride(stride), m_count(count), m_sz(stride * count)
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
    friend class MetalDataFactoryImpl;
    friend struct MetalCommandQueue;
    MetalCommandQueue* m_q;
    std::unique_ptr<uint8_t[]> m_cpuBuf;
    int m_validSlots = 0;
    MetalGraphicsBufferD(IGraphicsData* parent, MetalCommandQueue* q, BufferUse use,
                         MetalContext* ctx, size_t stride, size_t count)
    : boo::IGraphicsBufferD(parent), m_q(q), m_stride(stride), m_count(count), m_sz(stride * count)
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
    MetalTextureS(IGraphicsData* parent, MetalContext* ctx, size_t width, size_t height, size_t mips,
                  TextureFormat fmt, const void* data, size_t sz)
    : ITextureS(parent)
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
                if (width > 1)
                    width /= 2;
                if (height > 1)
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
    MetalTextureSA(IGraphicsData* parent, MetalContext* ctx, size_t width,
                   size_t height, size_t layers, size_t mips,
                   TextureFormat fmt, const void* data, size_t sz)
    : ITextureSA(parent)
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
                                                           mipmapped:(mips>1)?YES:NO];
            desc.textureType = MTLTextureType2DArray;
            desc.arrayLength = layers;
            desc.mipmapLevelCount = mips;
            desc.usage = MTLTextureUsageShaderRead;
            m_tex = [ctx->m_dev newTextureWithDescriptor:desc];
            const uint8_t* dataIt = reinterpret_cast<const uint8_t*>(data);
            for (size_t i=0 ; i<mips ; ++i)
            {
                for (size_t j=0 ; j<layers ; ++j)
                {
                    [m_tex replaceRegion:MTLRegionMake2D(0, 0, width, height)
                             mipmapLevel:i
                                   slice:j
                               withBytes:dataIt
                             bytesPerRow:width * ppitch
                           bytesPerImage:width * height * ppitch];
                    dataIt += width * height * ppitch;
                }
                if (width > 1)
                    width /= 2;
                if (height > 1)
                    height /= 2;
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
    MetalTextureD(IGraphicsData* parent, MetalCommandQueue* q, MetalContext* ctx,
                  size_t width, size_t height, TextureFormat fmt)
    : boo::ITextureD(parent), m_q(q), m_width(width), m_height(height)
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

#define MAX_BIND_TEXS 4

class MetalTextureR : public ITextureR
{
    friend class MetalDataFactory;
    friend struct MetalCommandQueue;
    size_t m_width = 0;
    size_t m_height = 0;
    size_t m_samples = 0;
    size_t m_colorBindCount;
    size_t m_depthBindCount;

    void Setup(MetalContext* ctx, size_t width, size_t height, size_t samples,
               size_t colorBindCount, size_t depthBindCount)
    {
        m_width = width;
        m_height = height;

        if (colorBindCount > MAX_BIND_TEXS)
            Log.report(logvisor::Fatal, "too many color bindings for render texture");
        if (depthBindCount > MAX_BIND_TEXS)
            Log.report(logvisor::Fatal, "too many depth bindings for render texture");

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

                if (colorBindCount)
                {
                    desc.usage = MTLTextureUsageShaderRead;
                    for (int i=0 ; i<colorBindCount ; ++i)
                        m_colorBindTex[i] = [ctx->m_dev newTextureWithDescriptor:desc];
                }

                desc.usage = MTLTextureUsageRenderTarget;
                desc.pixelFormat = MTLPixelFormatDepth32Float;
                m_depthTex = [ctx->m_dev newTextureWithDescriptor:desc];

                if (depthBindCount)
                {
                    desc.usage = MTLTextureUsageShaderRead;
                    for (int i=0 ; i<depthBindCount ; ++i)
                        m_depthBindTex[i] = [ctx->m_dev newTextureWithDescriptor:desc];
                }
            }
            else
            {
                desc.textureType = MTLTextureType2D;
                desc.sampleCount = 1;
                desc.usage = MTLTextureUsageRenderTarget;
                m_colorTex = [ctx->m_dev newTextureWithDescriptor:desc];

                if (colorBindCount)
                {
                    desc.usage = MTLTextureUsageShaderRead;
                    for (int i=0 ; i<colorBindCount ; ++i)
                        m_colorBindTex[i] = [ctx->m_dev newTextureWithDescriptor:desc];
                }

                desc.usage = MTLTextureUsageRenderTarget;
                desc.pixelFormat = MTLPixelFormatDepth32Float;
                m_depthTex = [ctx->m_dev newTextureWithDescriptor:desc];

                if (depthBindCount)
                {
                    desc.usage = MTLTextureUsageShaderRead;
                    for (int i=0 ; i<depthBindCount ; ++i)
                        m_depthBindTex[i] = [ctx->m_dev newTextureWithDescriptor:desc];
                }
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
                m_clearDepthPassDesc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);

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
                m_clearBothPassDesc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);

                m_clearBothPassDesc.depthAttachment.texture = m_depthTex;
                m_clearBothPassDesc.depthAttachment.loadAction = MTLLoadActionClear;
                m_clearBothPassDesc.depthAttachment.storeAction = MTLStoreActionStore;
                m_clearBothPassDesc.depthAttachment.clearDepth = 0.f;
            }
        }
    }

    MetalTextureR(IGraphicsData* parent, MetalContext* ctx, size_t width, size_t height, size_t samples,
                  size_t colorBindCount, size_t depthBindCount)
    : boo::ITextureR(parent), m_width(width), m_height(height), m_samples(samples),
      m_colorBindCount(colorBindCount),
      m_depthBindCount(depthBindCount)
    {
        if (samples == 0) m_samples = 1;
        Setup(ctx, width, height, samples, colorBindCount, depthBindCount);
    }
public:
    size_t samples() const {return m_samples;}
    id<MTLTexture> m_colorTex;
    id<MTLTexture> m_depthTex;
    id<MTLTexture> m_colorBindTex[MAX_BIND_TEXS] = {};
    id<MTLTexture> m_depthBindTex[MAX_BIND_TEXS] = {};
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
    size_t m_stride = 0;
    size_t m_instStride = 0;
    MetalVertexFormat(IGraphicsData* parent, size_t elementCount, const VertexElementDescriptor* elements)
    : boo::IVertexFormat(parent), m_elementCount(elementCount)
    {
        for (size_t i=0 ; i<elementCount ; ++i)
        {
            const VertexElementDescriptor* elemin = &elements[i];
            int semantic = int(elemin->semantic & VertexSemantic::SemanticMask);
            if ((elemin->semantic & VertexSemantic::Instanced) != VertexSemantic::None)
                m_instStride += SEMANTIC_SIZE_TABLE[semantic];
            else
                m_stride += SEMANTIC_SIZE_TABLE[semantic];
        }

        m_vdesc = [MTLVertexDescriptor vertexDescriptor];
        MTLVertexBufferLayoutDescriptor* layoutDesc = m_vdesc.layouts[0];
        layoutDesc.stride = m_stride;
        layoutDesc.stepFunction = MTLVertexStepFunctionPerVertex;
        layoutDesc.stepRate = 1;

        layoutDesc = m_vdesc.layouts[1];
        layoutDesc.stride = m_instStride;
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
    MTLBlendFactorOneMinusDestinationAlpha,
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 101200
    MTLBlendFactorSource1Color,
    MTLBlendFactorOneMinusSource1Color,
#else
    MTLBlendFactorSourceColor,
    MTLBlendFactorOneMinusSourceColor,
#endif
};

static const MTLPrimitiveType PRIMITIVE_TABLE[] =
{
    MTLPrimitiveTypeTriangle,
    MTLPrimitiveTypeTriangleStrip
};

#define COLOR_WRITE_MASK (MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue)

class MetalShaderPipeline : public IShaderPipeline
{
    friend class MetalDataFactory;
    friend struct MetalCommandQueue;
    friend struct MetalShaderDataBinding;
    MTLCullMode m_cullMode = MTLCullModeNone;
    MTLPrimitiveType m_drawPrim;
    const MetalVertexFormat* m_vtxFmt;
    MetalShareableShader::Token m_vert;
    MetalShareableShader::Token m_frag;

    MetalShaderPipeline(IGraphicsData* parent,
                        MetalContext* ctx,
                        MetalShareableShader::Token&& vert,
                        MetalShareableShader::Token&& frag,
                        const MetalVertexFormat* vtxFmt, NSUInteger targetSamples,
                        BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                        ZTest depthTest, bool depthWrite, bool colorWrite,
                        bool alphaWrite, CullMode culling)
    : boo::IShaderPipeline(parent),
      m_drawPrim(PRIMITIVE_TABLE[int(prim)]), m_vtxFmt(vtxFmt),
      m_vert(std::move(vert)), m_frag(std::move(frag))
    {
        switch (culling)
        {
        case CullMode::None:
        default:
            m_cullMode = MTLCullModeNone;
            break;
        case CullMode::Backface:
            m_cullMode = MTLCullModeBack;
            break;
        case CullMode::Frontface:
            m_cullMode = MTLCullModeFront;
            break;
        }

        MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
        desc.vertexFunction = m_vert.get().m_shader;
        desc.fragmentFunction = m_frag.get().m_shader;
        desc.vertexDescriptor = vtxFmt->m_vdesc;
        desc.sampleCount = targetSamples;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[0].writeMask = (colorWrite ? COLOR_WRITE_MASK : 0) |
                                             (alphaWrite ? MTLColorWriteMaskAlpha : 0);
        desc.colorAttachments[0].blendingEnabled = dstFac != BlendFactor::Zero;
        if (srcFac == BlendFactor::Subtract || dstFac == BlendFactor::Subtract)
        {
            desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorDestinationColor;
            desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorSourceColor;
            desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationSubtract;
        }
        else
        {
            desc.colorAttachments[0].sourceRGBBlendFactor = BLEND_FACTOR_TABLE[int(srcFac)];
            desc.colorAttachments[0].destinationRGBBlendFactor = BLEND_FACTOR_TABLE[int(dstFac)];
            desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        }
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorZero;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        desc.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
        NSError* err = nullptr;
        m_state = [ctx->m_dev newRenderPipelineStateWithDescriptor:desc error:&err];
        if (err)
            Log.report(logvisor::Fatal, "error making shader pipeline: %s",
                       [[err localizedDescription] UTF8String]);

        MTLDepthStencilDescriptor* dsDesc = [MTLDepthStencilDescriptor new];
        switch (depthTest)
        {
        case ZTest::None:
        default:
            dsDesc.depthCompareFunction = MTLCompareFunctionAlways;
            break;
        case ZTest::LEqual:
            dsDesc.depthCompareFunction = MTLCompareFunctionGreaterEqual;
            break;
        case ZTest::Greater:
            dsDesc.depthCompareFunction = MTLCompareFunctionLess;
            break;
        case ZTest::GEqual:
            dsDesc.depthCompareFunction = MTLCompareFunctionLessEqual;
            break;
        case ZTest::Equal:
            dsDesc.depthCompareFunction = MTLCompareFunctionEqual;
            break;
        }

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

static id<MTLBuffer> GetBufferGPUResource(const IGraphicsBuffer* buf, int idx, size_t& strideOut)
{
    if (buf->dynamic())
    {
        const MetalGraphicsBufferD* cbuf = static_cast<const MetalGraphicsBufferD*>(buf);
        strideOut = cbuf->m_stride;
        return cbuf->m_bufs[idx];
    }
    else
    {
        const MetalGraphicsBufferS* cbuf = static_cast<const MetalGraphicsBufferS*>(buf);
        strideOut = cbuf->m_stride;
        return cbuf->m_buf;
    }
}

static id<MTLTexture> GetTextureGPUResource(const ITexture* tex, int idx, int bindIdx, bool depth)
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
        return depth ? ctex->m_depthBindTex[bindIdx] : ctex->m_colorBindTex[bindIdx];
    }
    default: break;
    }
    return nullptr;
}

struct MetalShaderDataBinding : IShaderDataBindingPriv
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
    struct BoundTex
    {
        ITexture* tex;
        int idx;
        bool depth;
    };
    std::unique_ptr<BoundTex[]> m_texs;
    size_t m_baseVert;
    size_t m_baseInst;

    MetalShaderDataBinding(MetalData* d,
                           MetalContext* ctx,
                           IShaderPipeline* pipeline,
                           IGraphicsBuffer* vbuf, IGraphicsBuffer* instVbo, IGraphicsBuffer* ibuf,
                           size_t ubufCount, IGraphicsBuffer** ubufs, const PipelineStage* ubufStages,
                           const size_t* ubufOffs, const size_t* ubufSizes,
                           size_t texCount, ITexture** texs,
                           const int* texBindIdxs, const bool* depthBind,
                           size_t baseVert, size_t baseInst)
    : IShaderDataBindingPriv(d),
    m_pipeline(static_cast<MetalShaderPipeline*>(pipeline)),
    m_vbuf(vbuf),
    m_instVbo(instVbo),
    m_ibuf(ibuf),
    m_ubufCount(ubufCount),
    m_ubufs(new IGraphicsBuffer*[ubufCount]),
    m_texCount(texCount),
    m_texs(new BoundTex[texCount]),
    m_baseVert(baseVert),
    m_baseInst(baseInst)
    {
        addDepData(m_pipeline->m_parentData);
        
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
            if (ubufs[i])
                addDepData(ubufs[i]->m_parentData);
        }
        for (size_t i=0 ; i<texCount ; ++i)
        {
            m_texs[i] = {texs[i], texBindIdxs ? texBindIdxs[i] : 0, depthBind ? depthBind[i] : false};
            if (texs[i])
                addDepData(texs[i]->m_parentData);
        }
    }

    void bind(id<MTLRenderCommandEncoder> enc, int b)
    {
        m_pipeline->bind(enc);

        size_t stride;
        if (m_vbuf)
        {
            id<MTLBuffer> buf = GetBufferGPUResource(m_vbuf, b, stride);
            [enc setVertexBuffer:buf offset:stride * m_baseVert atIndex:0];
        }
        if (m_instVbo)
        {
            id<MTLBuffer> buf = GetBufferGPUResource(m_instVbo, b, stride);
            [enc setVertexBuffer:buf offset:stride * m_baseInst atIndex:1];
        }
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
            if (m_texs[i].tex)
                [enc setFragmentTexture:GetTextureGPUResource(m_texs[i].tex, b, m_texs[i].idx, m_texs[i].depth) atIndex:i];
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
        @autoreleasepool
        {
            [m_enc endEncoding];
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
        if (ctarget == m_boundTarget)
        {
            if (m_boundVp.width || m_boundVp.height)
                [m_enc setViewport:m_boundVp];
            if (m_boundScissor.width || m_boundScissor.height)
                [m_enc setScissorRect:m_boundScissor];
        }
        else
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

    void resolveBindTexture(ITextureR* texture, const SWindowRect& rect, bool tlOrigin,
                            int bindIdx, bool color, bool depth)
    {
        MetalTextureR* tex = static_cast<MetalTextureR*>(texture);
        @autoreleasepool
        {
            [m_enc endEncoding];
            SWindowRect intersectRect = rect.intersect(SWindowRect(0, 0, tex->m_width, tex->m_height));
            NSUInteger y = tlOrigin ? intersectRect.location[1] : int(tex->m_height) -
                    intersectRect.location[1] - intersectRect.size[1];
            MTLOrigin origin = {NSUInteger(intersectRect.location[0]), y, 0};
            id<MTLBlitCommandEncoder> blitEnc = [m_cmdBuf blitCommandEncoder];

            if (color && tex->m_colorBindTex[bindIdx])
            {
                [blitEnc copyFromTexture:tex->m_colorTex
                             sourceSlice:0
                             sourceLevel:0
                            sourceOrigin:origin
                              sourceSize:MTLSizeMake(intersectRect.size[0], intersectRect.size[1], 1)
                               toTexture:tex->m_colorBindTex[bindIdx]
                        destinationSlice:0
                        destinationLevel:0
                       destinationOrigin:origin];
            }

            if (depth && tex->m_depthBindTex[bindIdx])
            {
                [blitEnc copyFromTexture:tex->m_depthTex
                             sourceSlice:0
                             sourceLevel:0
                            sourceOrigin:origin
                              sourceSize:MTLSizeMake(intersectRect.size[0], intersectRect.size[1], 1)
                               toTexture:tex->m_depthBindTex[bindIdx]
                        destinationSlice:0
                        destinationLevel:0
                       destinationOrigin:origin];
            }

            [blitEnc endEncoding];
            m_enc = [m_cmdBuf renderCommandEncoderWithDescriptor:tex->m_passDesc];
            [m_enc setFrontFacingWinding:MTLWindingCounterClockwise];

            if (m_boundVp.width || m_boundVp.height)
                [m_enc setViewport:m_boundVp];
            if (m_boundScissor.width || m_boundScissor.height)
                [m_enc setScissorRect:m_boundScissor];
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
        MetalDataFactoryImpl* gfxF = static_cast<MetalDataFactoryImpl*>(m_parent->getDataFactory());
        std::unique_lock<std::mutex> datalk(gfxF->m_committedMutex);
        for (MetalData* d : gfxF->m_committedData)
        {
            for (std::unique_ptr<MetalGraphicsBufferD>& b : d->m_DBufs)
                b->update(m_fillBuf);
            for (std::unique_ptr<MetalTextureD>& t : d->m_DTexs)
                t->update(m_fillBuf);
        }
        for (MetalPool* p : gfxF->m_committedPools)
        {
            for (auto& b : p->m_items)
                b->m_buf->update(m_fillBuf);
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

MetalDataFactoryImpl::MetalDataFactoryImpl(IGraphicsContext* parent, MetalContext* ctx, uint32_t sampleCount)
: m_parent(parent), m_ctx(ctx), m_sampleCount(sampleCount) {}

IGraphicsBufferS* MetalDataFactory::Context::newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count)
{
    MetalData* d = MetalDataFactoryImpl::m_deferredData.get();
    MetalDataFactoryImpl& factory = static_cast<MetalDataFactoryImpl&>(m_parent);
    MetalGraphicsBufferS* retval = new MetalGraphicsBufferS(d, use, factory.m_ctx, data, stride, count);
    d->m_SBufs.emplace_back(retval);
    return retval;
}
IGraphicsBufferD* MetalDataFactory::Context::newDynamicBuffer(BufferUse use, size_t stride, size_t count)
{
    MetalData* d = MetalDataFactoryImpl::m_deferredData.get();
    MetalDataFactoryImpl& factory = static_cast<MetalDataFactoryImpl&>(m_parent);
    MetalCommandQueue* q = static_cast<MetalCommandQueue*>(factory.m_parent->getCommandQueue());
    MetalGraphicsBufferD* retval = new MetalGraphicsBufferD(d, q, use, factory.m_ctx, stride, count);
    d->m_DBufs.emplace_back(retval);
    return retval;
}

ITextureS* MetalDataFactory::Context::newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                                       const void* data, size_t sz)
{
    MetalData* d = MetalDataFactoryImpl::m_deferredData.get();
    MetalDataFactoryImpl& factory = static_cast<MetalDataFactoryImpl&>(m_parent);
    MetalTextureS* retval = new MetalTextureS(d, factory.m_ctx, width, height, mips, fmt, data, sz);
    d->m_STexs.emplace_back(retval);
    return retval;
}
ITextureSA* MetalDataFactory::Context::newStaticArrayTexture(size_t width, size_t height, size_t layers, size_t mips,
                                                             TextureFormat fmt, const void* data, size_t sz)
{
    MetalData* d = MetalDataFactoryImpl::m_deferredData.get();
    MetalDataFactoryImpl& factory = static_cast<MetalDataFactoryImpl&>(m_parent);
    MetalTextureSA* retval = new MetalTextureSA(d, factory.m_ctx, width, height, layers, mips, fmt, data, sz);
    d->m_SATexs.emplace_back(retval);
    return retval;
}
ITextureD* MetalDataFactory::Context::newDynamicTexture(size_t width, size_t height, TextureFormat fmt)
{
    MetalData* d = MetalDataFactoryImpl::m_deferredData.get();
    MetalDataFactoryImpl& factory = static_cast<MetalDataFactoryImpl&>(m_parent);
    MetalCommandQueue* q = static_cast<MetalCommandQueue*>(factory.m_parent->getCommandQueue());
    MetalTextureD* retval = new MetalTextureD(d, q, factory.m_ctx, width, height, fmt);
    d->m_DTexs.emplace_back(retval);
    return retval;
}
ITextureR* MetalDataFactory::Context::newRenderTexture(size_t width, size_t height,
                                                       size_t colorBindCount, size_t depthBindCount)
{
    MetalData* d = MetalDataFactoryImpl::m_deferredData.get();
    MetalDataFactoryImpl& factory = static_cast<MetalDataFactoryImpl&>(m_parent);
    MetalTextureR* retval = new MetalTextureR(d, factory.m_ctx, width, height, factory.m_sampleCount,
                                              colorBindCount, depthBindCount);
    d->m_RTexs.emplace_back(retval);
    return retval;
}

IVertexFormat* MetalDataFactory::Context::newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements,
                                                          size_t baseVert, size_t baseInst)
{
    MetalData* d = MetalDataFactoryImpl::m_deferredData.get();
    MetalVertexFormat* retval = new struct MetalVertexFormat(d, elementCount, elements);
    d->m_VFmts.emplace_back(retval);
    return retval;
}

IShaderPipeline* MetalDataFactory::Context::newShaderPipeline(const char* vertSource, const char* fragSource,
                                                              IVertexFormat* vtxFmt, unsigned targetSamples,
                                                              BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                                                              ZTest depthTest, bool depthWrite, bool colorWrite,
                                                              bool alphaWrite, CullMode culling)
{
    @autoreleasepool
    {
        MetalData* d = MetalDataFactoryImpl::m_deferredData.get();
        MetalDataFactoryImpl& factory = static_cast<MetalDataFactoryImpl&>(m_parent);
        MTLCompileOptions* compOpts = [MTLCompileOptions new];
        compOpts.languageVersion = MTLLanguageVersion1_1;
        NSError* err = nullptr;

        XXH64_state_t hashState;
        uint64_t hashes[2];
        XXH64_reset(&hashState, 0);
        XXH64_update(&hashState, vertSource, strlen(vertSource));
        hashes[0] = XXH64_digest(&hashState);
        XXH64_reset(&hashState, 0);
        XXH64_update(&hashState, fragSource, strlen(fragSource));
        hashes[1] = XXH64_digest(&hashState);

        MetalShareableShader::Token vertShader;
        MetalShareableShader::Token fragShader;
        auto vertFind = factory.m_sharedShaders.find(hashes[0]);
        if (vertFind != factory.m_sharedShaders.end())
        {
            vertShader = vertFind->second->lock();
        }
        else
        {
            id<MTLLibrary> vertShaderLib = [factory.m_ctx->m_dev newLibraryWithSource:@(vertSource)
                                                                              options:compOpts
                                                                                error:&err];
            if (!vertShaderLib)
            {
                printf("%s\n", vertSource);
                Log.report(logvisor::Fatal, "error compiling vert shader: %s", [[err localizedDescription] UTF8String]);
            }
            id<MTLFunction> vertFunc = [vertShaderLib newFunctionWithName:@"vmain"];

            auto it =
            factory.m_sharedShaders.emplace(std::make_pair(hashes[0],
                std::make_unique<MetalShareableShader>(factory, hashes[0], vertFunc))).first;
            vertShader = it->second->lock();
        }
        auto fragFind = factory.m_sharedShaders.find(hashes[1]);
        if (fragFind != factory.m_sharedShaders.end())
        {
            fragShader = fragFind->second->lock();
        }
        else
        {
            id<MTLLibrary> fragShaderLib = [factory.m_ctx->m_dev newLibraryWithSource:@(fragSource)
                                                                              options:compOpts
                                                                                error:&err];
            if (!fragShaderLib)
            {
                printf("%s\n", fragSource);
                Log.report(logvisor::Fatal, "error compiling frag shader: %s", [[err localizedDescription] UTF8String]);
            }
            id<MTLFunction> fragFunc = [fragShaderLib newFunctionWithName:@"fmain"];

            auto it =
            factory.m_sharedShaders.emplace(std::make_pair(hashes[1],
                std::make_unique<MetalShareableShader>(factory, hashes[1], fragFunc))).first;
            fragShader = it->second->lock();
        }

        MetalShaderPipeline* retval = new MetalShaderPipeline(d, factory.m_ctx, std::move(vertShader), std::move(fragShader),
                                                              static_cast<const MetalVertexFormat*>(vtxFmt), targetSamples,
                                                              srcFac, dstFac, prim, depthTest, depthWrite,
                                                              colorWrite, alphaWrite, culling);
        d->m_SPs.emplace_back(retval);
        return retval;
    }
}

IShaderDataBinding*
MetalDataFactory::Context::newShaderDataBinding(IShaderPipeline* pipeline,
                                                IVertexFormat* vtxFormat,
                                                IGraphicsBuffer* vbuf, IGraphicsBuffer* instVbo, IGraphicsBuffer* ibuf,
                                                size_t ubufCount, IGraphicsBuffer** ubufs, const PipelineStage* ubufStages,
                                                const size_t* ubufOffs, const size_t* ubufSizes,
                                                size_t texCount, ITexture** texs,
                                                const int* texBindIdxs, const bool* depthBind,
                                                size_t baseVert, size_t baseInst)
{
    MetalDataFactoryImpl& factory = static_cast<MetalDataFactoryImpl&>(m_parent);
    MetalShaderDataBinding* retval =
    new MetalShaderDataBinding(MetalDataFactoryImpl::m_deferredData.get(),
                               factory.m_ctx, pipeline, vbuf, instVbo, ibuf,
                               ubufCount, ubufs, ubufStages, ubufOffs,
                               ubufSizes, texCount, texs, texBindIdxs,
                               depthBind, baseVert, baseInst);
    MetalDataFactoryImpl::m_deferredData->m_SBinds.emplace_back(retval);
    return retval;
}

GraphicsDataToken MetalDataFactoryImpl::commitTransaction(const FactoryCommitFunc& trans)
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

GraphicsBufferPoolToken MetalDataFactoryImpl::newBufferPool()
{
    std::unique_lock<std::mutex> lk(m_committedMutex);
    MetalPool* retval = new MetalPool;
    m_committedPools.insert(retval);
    return GraphicsBufferPoolToken(this, retval);
}

void MetalDataFactoryImpl::destroyData(IGraphicsData* d)
{
    std::unique_lock<std::mutex> lk(m_committedMutex);
    MetalData* data = static_cast<MetalData*>(d);
    m_committedData.erase(data);
    data->decrement();
}

void MetalDataFactoryImpl::destroyAllData()
{
    std::unique_lock<std::mutex> lk(m_committedMutex);
    for (MetalData* data : m_committedData)
        data->decrement();
    for (MetalPool* pool : m_committedPools)
        delete pool;
    m_committedData.clear();
    m_committedPools.clear();
}

void MetalDataFactoryImpl::destroyPool(IGraphicsBufferPool* p)
{
    std::unique_lock<std::mutex> lk(m_committedMutex);
    MetalPool* pool = static_cast<MetalPool*>(p);
    m_committedPools.erase(pool);
    delete pool;
}

IGraphicsBufferD* MetalDataFactoryImpl::newPoolBuffer(IGraphicsBufferPool* p, BufferUse use,
                                                      size_t stride, size_t count)
{
    MetalPool* pool = static_cast<MetalPool*>(p);
    MetalCommandQueue* q = static_cast<MetalCommandQueue*>(m_parent->getCommandQueue());
    MetalPoolItem* item = new MetalPoolItem;
    MetalGraphicsBufferD* retval = new MetalGraphicsBufferD(item, q, use, m_ctx, stride, count);
    item->m_buf.reset(retval);
    pool->m_items.emplace(item);
    return retval;
}

void MetalDataFactoryImpl::deletePoolBuffer(IGraphicsBufferPool* p, IGraphicsBufferD* buf)
{
    MetalPool* pool = static_cast<MetalPool*>(p);
    auto search = pool->m_items.find(static_cast<MetalPoolItem*>(buf->m_parentData));
    if (search != pool->m_items.end())
    {
        (*search)->decrement();
        pool->m_items.erase(search);
    }
}

IGraphicsCommandQueue* _NewMetalCommandQueue(MetalContext* ctx, IWindow* parentWindow,
                                             IGraphicsContext* parent)
{
    return new struct MetalCommandQueue(ctx, parentWindow, parent);
}

IGraphicsDataFactory* _NewMetalDataFactory(IGraphicsContext* parent, MetalContext* ctx, uint32_t sampleCount)
{
    return new class MetalDataFactoryImpl(parent, ctx, sampleCount);
}

}

#endif
