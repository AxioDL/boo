#include "../mac/CocoaCommon.hpp"
#if BOO_HAS_METAL
#include "logvisor/logvisor.hpp"
#include "boo/IApplication.hpp"
#include "boo/graphicsdev/Metal.hpp"
#include "boo/IGraphicsContext.hpp"
#include "Common.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "xxhash/xxhash.h"

#if !__has_feature(objc_arc)
#error ARC Required
#endif

#define MAX_UNIFORM_COUNT 8
#define MAX_TEXTURE_COUNT 8

static const char* GammaVS =
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct VertData\n"
"{\n"
"    float4 posIn [[ attribute(0) ]];\n"
"    float4 uvIn [[ attribute(1) ]];\n"
"};\n"
"\n"
"struct VertToFrag\n"
"{\n"
"    float4 pos [[ position ]];\n"
"    float2 uv;\n"
"};\n"
"\n"
"vertex VertToFrag vmain(VertData v [[ stage_in ]])\n"
"{\n"
"    VertToFrag vtf;\n"
"    vtf.uv = v.uvIn.xy;\n"
"    vtf.pos = v.posIn;\n"
"    return vtf;\n"
"}\n";

static const char* GammaFS =
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct VertToFrag\n"
"{\n"
"    float4 pos [[ position ]];\n"
"    float2 uv;\n"
"};\n"
"\n"
"fragment float4 fmain(VertToFrag vtf [[ stage_in ]],\n"
"                      sampler clampSamp [[ sampler(3) ]],\n"
"                      texture2d<float> screenTex [[ texture(0) ]],\n"
"                      texture2d<float> gammaLUT [[ texture(1) ]])\n"
"{\n"
"    uint4 tex = uint4(saturate(screenTex.sample(clampSamp, vtf.uv)) * float4(65535.0));\n"
"    float4 colorOut;\n"
"    for (int i=0 ; i<3 ; ++i)\n"
"        colorOut[i] = gammaLUT.read(uint2(tex[i] % 256, tex[i] / 256)).r;\n"
"    return colorOut;\n"
"}\n";

namespace boo
{
static logvisor::Module Log("boo::Metal");
struct MetalCommandQueue;
class MetalDataFactoryImpl;

class MetalDataFactoryImpl : public MetalDataFactory, public GraphicsDataFactoryHead
{
    friend struct MetalCommandQueue;
    friend class MetalDataFactory::Context;
    IGraphicsContext* m_parent;
    struct MetalContext* m_ctx;

    bool m_hasTessellation = false;

    float m_gamma = 1.f;
    ObjToken<IShaderPipeline> m_gammaShader;
    ObjToken<ITextureD> m_gammaLUT;
    ObjToken<IGraphicsBufferS> m_gammaVBO;
    ObjToken<IShaderDataBinding> m_gammaBinding;
    void SetupGammaResources()
    {
        m_hasTessellation = [m_ctx->m_dev supportsFeatureSet:MTLFeatureSet_macOS_GPUFamily1_v2];

        commitTransaction([this](IGraphicsDataFactory::Context& ctx)
        {
            auto vertexMetal = MetalDataFactory::CompileMetal(GammaVS, PipelineStage::Vertex);
            auto vertexShader = ctx.newShaderStage(vertexMetal, PipelineStage::Vertex);
            auto fragmentMetal = MetalDataFactory::CompileMetal(GammaFS, PipelineStage::Fragment);
            auto fragmentShader = ctx.newShaderStage(fragmentMetal, PipelineStage::Fragment);
            const VertexElementDescriptor vfmt[] = {
                {VertexSemantic::Position4},
                {VertexSemantic::UV4}
            };
            AdditionalPipelineInfo info =
                {
                    BlendFactor::One, BlendFactor::Zero,
                    Primitive::TriStrips, ZTest::None, false, true, false, CullMode::None
                };
            m_gammaShader = ctx.newShaderPipeline(vertexShader, fragmentShader, vfmt, info);
            m_gammaLUT = ctx.newDynamicTexture(256, 256, TextureFormat::I16, TextureClampMode::ClampToEdge);
            setDisplayGamma(1.f);
            const struct Vert {
                float pos[4];
                float uv[4];
            } verts[4] = {
                {{-1.f,  1.f, 0.f, 1.f}, {0.f, 0.f, 0.f, 0.f}},
                {{ 1.f,  1.f, 0.f, 1.f}, {1.f, 0.f, 0.f, 0.f}},
                {{-1.f, -1.f, 0.f, 1.f}, {0.f, 1.f, 0.f, 0.f}},
                {{ 1.f, -1.f, 0.f, 1.f}, {1.f, 1.f, 0.f, 0.f}}
            };
            m_gammaVBO = ctx.newStaticBuffer(BufferUse::Vertex, verts, 32, 4);
            ObjToken<ITexture> texs[] = {{}, m_gammaLUT.get()};
            m_gammaBinding = ctx.newShaderDataBinding(m_gammaShader, m_gammaVBO.get(), {}, {},
                                                      0, nullptr, nullptr, 2, texs, nullptr, nullptr);
            return true;
        } BooTrace);
    }

public:

    MetalDataFactoryImpl(IGraphicsContext* parent, MetalContext* ctx)
    : m_parent(parent), m_ctx(ctx) {}
    ~MetalDataFactoryImpl() = default;

    Platform platform() const { return Platform::Metal; }
    const char* platformName() const { return "Metal"; }
    void commitTransaction(const std::function<bool(IGraphicsDataFactory::Context& ctx)>& __BooTraceArgs);
    ObjToken<IGraphicsBufferD> newPoolBuffer(BufferUse use, size_t stride, size_t count __BooTraceArgs);

    void setDisplayGamma(float gamma)
    {
        if (m_ctx->m_pixelFormat == MTLPixelFormatRGBA16Float)
            m_gamma = gamma * 2.2f;
        else
            m_gamma = gamma;
        if (m_gamma != 1.f)
            UpdateGammaLUT(m_gammaLUT.get(), m_gamma);
    }

    bool isTessellationSupported(uint32_t& maxPatchSize)
    {
        maxPatchSize = 32;
        return m_hasTessellation;
    }
};

#define MTL_STATIC MTLResourceCPUCacheModeWriteCombined|MTLResourceStorageModeManaged
#define MTL_DYNAMIC MTLResourceCPUCacheModeWriteCombined|MTLResourceStorageModeManaged

class MetalGraphicsBufferS : public GraphicsDataNode<IGraphicsBufferS>
{
    friend class MetalDataFactory;
    friend struct MetalCommandQueue;
    MetalGraphicsBufferS(const ObjToken<BaseGraphicsData>& parent, BufferUse use, MetalContext* ctx,
                         const void* data, size_t stride, size_t count)
    : GraphicsDataNode<IGraphicsBufferS>(parent), m_stride(stride), m_count(count), m_sz(stride * count)
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

template<class DataCls>
class MetalGraphicsBufferD : public GraphicsDataNode<IGraphicsBufferD, DataCls>
{
    friend class MetalDataFactory;
    friend class MetalDataFactoryImpl;
    friend struct MetalCommandQueue;
    MetalCommandQueue* m_q;
    std::unique_ptr<uint8_t[]> m_cpuBuf;
    int m_validSlots = 0;
    MetalGraphicsBufferD(const ObjToken<DataCls>& parent, MetalCommandQueue* q, BufferUse use,
                         MetalContext* ctx, size_t stride, size_t count)
    : GraphicsDataNode<IGraphicsBufferD, DataCls>(parent), m_q(q), m_stride(stride),
      m_count(count), m_sz(stride * count)
    {
        m_cpuBuf.reset(new uint8_t[m_sz]);
        m_bufs[0] = [ctx->m_dev newBufferWithLength:m_sz options:MTL_DYNAMIC];
        m_bufs[1] = [ctx->m_dev newBufferWithLength:m_sz options:MTL_DYNAMIC];
    }
public:
    size_t m_stride;
    size_t m_count;
    size_t m_sz;
    id<MTLBuffer> m_bufs[2];
    MetalGraphicsBufferD() = default;

    void update(int b)
    {
        int slot = 1 << b;
        if ((slot & m_validSlots) == 0)
        {
            id<MTLBuffer> res = m_bufs[b];
            memcpy(res.contents, m_cpuBuf.get(), m_sz);
            [res didModifyRange:NSMakeRange(0, m_sz)];
            m_validSlots |= slot;
        }
    }
    void load(const void* data, size_t sz)
    {
        size_t bufSz = std::min(sz, m_sz);
        memcpy(m_cpuBuf.get(), data, bufSz);
        m_validSlots = 0;
    }
    void* map(size_t sz)
    {
        if (sz > m_sz)
            return nullptr;
        return m_cpuBuf.get();
    }
    void unmap()
    {
        m_validSlots = 0;
    }
};

class MetalTextureS : public GraphicsDataNode<ITextureS>
{
    friend class MetalDataFactory;
    MetalTextureS(const ObjToken<BaseGraphicsData>& parent, MetalContext* ctx, size_t width, size_t height,
                  size_t mips, TextureFormat fmt, const void* data, size_t sz)
    : GraphicsDataNode<ITextureS>(parent)
    {
        MTLPixelFormat pfmt = MTLPixelFormatRGBA8Unorm;
        NSUInteger ppitchNum = 4;
        NSUInteger ppitchDenom = 1;
        NSUInteger bytesPerRow = width * ppitchNum;
        switch (fmt)
        {
        case TextureFormat::I8:
            pfmt = MTLPixelFormatR8Unorm;
            ppitchNum = 1;
            bytesPerRow = width * ppitchNum;
            break;
        case TextureFormat::I16:
            pfmt = MTLPixelFormatR16Unorm;
            ppitchNum = 2;
            bytesPerRow = width * ppitchNum;
            break;
        case TextureFormat::DXT1:
            pfmt = MTLPixelFormatBC1_RGBA;
            ppitchNum = 1;
            ppitchDenom = 2;
            bytesPerRow = width * 8 / 4; // Metal wants this in blocks, not bytes
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
                         bytesPerRow:bytesPerRow];
                dataIt += width * height * ppitchNum / ppitchDenom;
                if (width > 1)
                {
                    width /= 2;
                    bytesPerRow /= 2;
                }
                if (height > 1)
                    height /= 2;
            }
        }
    }
public:
    id<MTLTexture> m_tex;
    ~MetalTextureS() = default;
};

class MetalTextureSA : public GraphicsDataNode<ITextureSA>
{
    friend class MetalDataFactory;
    MetalTextureSA(const ObjToken<BaseGraphicsData>& parent, MetalContext* ctx, size_t width,
                   size_t height, size_t layers, size_t mips,
                   TextureFormat fmt, const void* data, size_t sz)
    : GraphicsDataNode<ITextureSA>(parent)
    {
        MTLPixelFormat pfmt = MTLPixelFormatRGBA8Unorm;
        NSUInteger ppitch = 4;
        switch (fmt)
        {
        case TextureFormat::I8:
            pfmt = MTLPixelFormatR8Unorm;
            ppitch = 1;
            break;
        case TextureFormat::I16:
            pfmt = MTLPixelFormatR16Unorm;
            ppitch = 2;
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

class MetalTextureD : public GraphicsDataNode<ITextureD>
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
    MetalTextureD(const ObjToken<BaseGraphicsData>& parent, MetalCommandQueue* q, MetalContext* ctx,
                  size_t width, size_t height, TextureFormat fmt)
    : GraphicsDataNode<ITextureD>(parent), m_q(q), m_width(width), m_height(height)
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
        case TextureFormat::I16:
            format = MTLPixelFormatR16Unorm;
            m_pxPitch = 2;
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
public:
    id<MTLTexture> m_texs[2];
    ~MetalTextureD() = default;

    void update(int b)
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
    void load(const void* data, size_t sz)
    {
        size_t bufSz = std::min(sz, m_cpuSz);
        memcpy(m_cpuBuf.get(), data, bufSz);
        m_validSlots = 0;
    }
    void* map(size_t sz)
    {
        if (sz > m_cpuSz)
            return nullptr;
        return m_cpuBuf.get();
    }
    void unmap()
    {
        m_validSlots = 0;
    }
};

#define MAX_BIND_TEXS 4

class MetalTextureR : public GraphicsDataNode<ITextureR>
{
    friend class MetalDataFactory;
    friend struct MetalCommandQueue;
    size_t m_width = 0;
    size_t m_height = 0;
    size_t m_samples = 0;
    size_t m_colorBindCount;
    size_t m_depthBindCount;

    void Setup(MetalContext* ctx)
    {
        if (m_colorBindCount > MAX_BIND_TEXS)
            Log.report(logvisor::Fatal, "too many color bindings for render texture");
        if (m_depthBindCount > MAX_BIND_TEXS)
            Log.report(logvisor::Fatal, "too many depth bindings for render texture");

        @autoreleasepool
        {
            MTLTextureDescriptor* desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:ctx->m_pixelFormat
                                                               width:m_width height:m_height
                                                           mipmapped:NO];
            desc.storageMode = MTLStorageModePrivate;

            if (m_samples > 1)
            {
                desc.textureType = MTLTextureType2DMultisample;
                desc.sampleCount = m_samples;
                desc.usage = MTLTextureUsageRenderTarget;
                m_colorTex = [ctx->m_dev newTextureWithDescriptor:desc];

                desc.pixelFormat = MTLPixelFormatDepth32Float;
                m_depthTex = [ctx->m_dev newTextureWithDescriptor:desc];
            }
            else
            {
                desc.textureType = MTLTextureType2D;
                desc.sampleCount = 1;
                desc.usage = MTLTextureUsageRenderTarget;
                m_colorTex = [ctx->m_dev newTextureWithDescriptor:desc];

                desc.pixelFormat = MTLPixelFormatDepth32Float;
                m_depthTex = [ctx->m_dev newTextureWithDescriptor:desc];
            }

            desc.textureType = MTLTextureType2D;
            desc.sampleCount = 1;
            desc.usage = MTLTextureUsageShaderRead;
            if (m_colorBindCount)
            {
                desc.pixelFormat = ctx->m_pixelFormat;
                for (int i=0 ; i<m_colorBindCount ; ++i)
                {
                    m_colorBindTex[i] = [ctx->m_dev newTextureWithDescriptor:desc];
                    if (m_samples > 1)
                    {
                        m_blitColor[i] = [MTLRenderPassDescriptor renderPassDescriptor];
                        m_blitColor[i].colorAttachments[0].texture = m_colorTex;
                        m_blitColor[i].colorAttachments[0].loadAction = MTLLoadActionLoad;
                        m_blitColor[i].colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
                        m_blitColor[i].colorAttachments[0].resolveTexture = m_colorBindTex[i];
                    }
                }
            }

            if (m_depthBindCount)
            {
                desc.pixelFormat = MTLPixelFormatDepth32Float;
                for (int i=0 ; i<m_depthBindCount ; ++i)
                {
                    m_depthBindTex[i] = [ctx->m_dev newTextureWithDescriptor:desc];
                    if (m_samples > 1)
                    {
                        m_blitDepth[i] = [MTLRenderPassDescriptor renderPassDescriptor];
                        m_blitDepth[i].depthAttachment.texture = m_colorTex;
                        m_blitDepth[i].depthAttachment.loadAction = MTLLoadActionLoad;
                        m_blitDepth[i].depthAttachment.storeAction = MTLStoreActionMultisampleResolve;
                        m_blitDepth[i].depthAttachment.resolveTexture = m_depthBindTex[i];
                    }
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

    MetalTextureR(const ObjToken<BaseGraphicsData>& parent, MetalContext* ctx, size_t width, size_t height,
                  size_t samples, size_t colorBindCount, size_t depthBindCount)
    : GraphicsDataNode<ITextureR>(parent), m_width(width), m_height(height), m_samples(samples),
      m_colorBindCount(colorBindCount),
      m_depthBindCount(depthBindCount)
    {
        if (samples == 0) m_samples = 1;
        Setup(ctx);
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
    MTLRenderPassDescriptor* m_blitColor[MAX_BIND_TEXS] = {};
    MTLRenderPassDescriptor* m_blitDepth[MAX_BIND_TEXS] = {};
    ~MetalTextureR() = default;

    void resize(MetalContext* ctx, size_t width, size_t height)
    {
        if (width < 1)
            width = 1;
        if (height < 1)
            height = 1;
        m_width = width;
        m_height = height;
        Setup(ctx);
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

struct MetalVertexFormat
{
    size_t m_elementCount;
    MTLVertexDescriptor* m_vdesc;
    size_t m_stride = 0;
    size_t m_instStride = 0;
    MetalVertexFormat(size_t elementCount, const VertexElementDescriptor* elements)
    : m_elementCount(elementCount)
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

    MTLStageInputOutputDescriptor* makeTessellationComputeLayout() const
    {
        MTLStageInputOutputDescriptor* ret = [MTLStageInputOutputDescriptor stageInputOutputDescriptor];

        MTLBufferLayoutDescriptor* layoutDesc = ret.layouts[0];
        layoutDesc.stride = m_stride;
        layoutDesc.stepFunction = MTLStepFunctionThreadPositionInGridX;
        layoutDesc.stepRate = 1;

        for (size_t i=0 ; i<m_elementCount ; ++i)
        {
            MTLVertexAttributeDescriptor* origAttrDesc = m_vdesc.attributes[i];
            MTLAttributeDescriptor* attrDesc = ret.attributes[i];
            attrDesc.format = MTLAttributeFormat(origAttrDesc.format);
            attrDesc.offset = origAttrDesc.offset;
            attrDesc.bufferIndex = origAttrDesc.bufferIndex;
        }

        return ret;
    }

    MTLVertexDescriptor* makeTessellationVertexLayout() const
    {
        MTLVertexDescriptor* ret = [MTLVertexDescriptor vertexDescriptor];

        MTLVertexBufferLayoutDescriptor* layoutDesc = ret.layouts[0];
        layoutDesc.stride = m_stride;
        layoutDesc.stepFunction = MTLVertexStepFunctionPerPatch;
        layoutDesc.stepRate = 1;

        for (size_t i=0 ; i<m_elementCount ; ++i)
        {
            MTLVertexAttributeDescriptor* origAttrDesc = m_vdesc.attributes[i];
            MTLVertexAttributeDescriptor* attrDesc = ret.attributes[i];
            attrDesc.format = origAttrDesc.format;
            attrDesc.offset = origAttrDesc.offset;
            attrDesc.bufferIndex = origAttrDesc.bufferIndex;
        }

        return ret;
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
    MTLPrimitiveTypeTriangleStrip,
    MTLPrimitiveTypePoint /* Actually patches */
};

#define COLOR_WRITE_MASK (MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue)

class MetalShaderStage : public GraphicsDataNode<IShaderStage>
{
    friend class MetalDataFactory;
    id<MTLFunction> m_shader;
    MetalShaderStage(const boo::ObjToken<BaseGraphicsData>& parent, MetalContext* ctx,
                     const uint8_t* data, size_t size, PipelineStage stage)
    : GraphicsDataNode<IShaderStage>(parent)
    {
        NSError* err = nullptr;

        id<MTLLibrary> shaderLib;
        if (data[0] == 1)
        {
            dispatch_data_t d = dispatch_data_create(data + 1, size - 1, nullptr, nullptr);
            shaderLib = [ctx->m_dev newLibraryWithData:d error:&err];
        }
        else
        {
            MTLCompileOptions* compOpts = [MTLCompileOptions new];
            compOpts.languageVersion = MTLLanguageVersion1_2;
            shaderLib = [ctx->m_dev newLibraryWithSource:@((const char*)(data + 1))
                                                 options:compOpts
                                                   error:&err];
            if (!shaderLib)
                printf("%s\n", data + 1);
        }
        if (!shaderLib)
            Log.report(logvisor::Fatal, "error creating library: %s", [[err localizedDescription] UTF8String]);

        NSString* funcName;
        switch (stage)
        {
        case PipelineStage::Vertex:
        default:
            funcName = @"vmain"; break;
        case PipelineStage::Fragment:
            funcName = @"fmain"; break;
        case PipelineStage::Geometry:
            funcName = @"gmain"; break;
        case PipelineStage::Control:
            funcName = @"cmain"; break;
        case PipelineStage::Evaluation:
            funcName = @"emain"; break;
        }
        m_shader = [shaderLib newFunctionWithName:funcName];
    }
public:
    id<MTLFunction> shader() const { return m_shader; }
};

class MetalShaderPipeline : public GraphicsDataNode<IShaderPipeline>
{
protected:
    friend class MetalDataFactory;
    friend struct MetalCommandQueue;
    friend struct MetalShaderDataBinding;
    MTLCullMode m_cullMode = MTLCullModeNone;
    MTLPrimitiveType m_drawPrim;
    uint32_t m_patchSize;

    MetalShaderPipeline(const boo::ObjToken<BaseGraphicsData>& parent)
    : GraphicsDataNode<IShaderPipeline>(parent) {}

    virtual void setupExtraStages(MetalContext* ctx, MTLRenderPipelineDescriptor* desc,
                                  ObjToken<IShaderStage> compute, const MetalVertexFormat& cVtxFmt) {}

    virtual void draw(MetalCommandQueue& q, size_t start, size_t count);
    virtual void drawIndexed(MetalCommandQueue& q, size_t start, size_t count);
    virtual void drawInstances(MetalCommandQueue& q, size_t start, size_t count, size_t instCount, size_t startInst);
    virtual void drawInstancesIndexed(MetalCommandQueue& q, size_t start, size_t count, size_t instCount, size_t startInst);

    void setup(MetalContext* ctx,
               NSUInteger targetSamples,
               ObjToken<IShaderStage> vertex,
               ObjToken<IShaderStage> fragment,
               ObjToken<IShaderStage> compute,
               const VertexFormatInfo& vtxFmt,
               const AdditionalPipelineInfo& info)
    {
        m_drawPrim = PRIMITIVE_TABLE[int(info.prim)];
        m_patchSize = info.patchSize;

        switch (info.culling)
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
        desc.vertexFunction = vertex.cast<MetalShaderStage>()->shader();
        desc.fragmentFunction = fragment.cast<MetalShaderStage>()->shader();
        MetalVertexFormat cVtxFmt(vtxFmt.elementCount, vtxFmt.elements);
        desc.vertexDescriptor = cVtxFmt.m_vdesc;
        setupExtraStages(ctx, desc, compute, cVtxFmt);
        desc.sampleCount = targetSamples;
        desc.colorAttachments[0].pixelFormat = ctx->m_pixelFormat;
        desc.colorAttachments[0].writeMask = (info.colorWrite ? COLOR_WRITE_MASK : 0) |
                                             (info.alphaWrite ? MTLColorWriteMaskAlpha : 0);
        desc.colorAttachments[0].blendingEnabled = info.dstFac != BlendFactor::Zero;
        if (info.srcFac == BlendFactor::Subtract || info.dstFac == BlendFactor::Subtract)
        {
            desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
            desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationReverseSubtract;
            if (info.overwriteAlpha)
            {
                desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
                desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorZero;
                desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
            }
            else
            {
                desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
                desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
                desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationReverseSubtract;
            }
        }
        else
        {
            desc.colorAttachments[0].sourceRGBBlendFactor = BLEND_FACTOR_TABLE[int(info.srcFac)];
            desc.colorAttachments[0].destinationRGBBlendFactor = BLEND_FACTOR_TABLE[int(info.dstFac)];
            desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
            if (info.overwriteAlpha)
            {
                desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
                desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorZero;
            }
            else
            {
                desc.colorAttachments[0].sourceAlphaBlendFactor = BLEND_FACTOR_TABLE[int(info.srcFac)];
                desc.colorAttachments[0].destinationAlphaBlendFactor = BLEND_FACTOR_TABLE[int(info.dstFac)];
            }
            desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        }
        desc.depthAttachmentPixelFormat = info.depthAttachment ? MTLPixelFormatDepth32Float : MTLPixelFormatInvalid;
        desc.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
        NSError* err = nullptr;
        m_state = [ctx->m_dev newRenderPipelineStateWithDescriptor:desc error:&err];
        if (err)
            Log.report(logvisor::Fatal, "error making shader pipeline: %s",
                       [[err localizedDescription] UTF8String]);

        MTLDepthStencilDescriptor* dsDesc = [MTLDepthStencilDescriptor new];
        switch (info.depthTest)
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

        dsDesc.depthWriteEnabled = info.depthWrite;
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

class MetalTessellationShaderPipeline : public MetalShaderPipeline
{
    friend class MetalDataFactory;
    friend struct MetalCommandQueue;
    friend struct MetalShaderDataBinding;

    MetalTessellationShaderPipeline(const ObjToken<BaseGraphicsData>& parent)
    : MetalShaderPipeline(parent) {}

    void setupExtraStages(MetalContext* ctx, MTLRenderPipelineDescriptor* desc,
                          ObjToken<IShaderStage> compute, const MetalVertexFormat& cVtxFmt)
    {
        desc.maxTessellationFactor = 16;
        desc.tessellationFactorScaleEnabled = NO;
        desc.tessellationFactorFormat = MTLTessellationFactorFormatHalf;
        desc.tessellationControlPointIndexType = MTLTessellationControlPointIndexTypeNone;
        desc.tessellationFactorStepFunction = MTLTessellationFactorStepFunctionPerPatch;
        desc.tessellationOutputWindingOrder = MTLWindingClockwise;
        desc.tessellationPartitionMode = MTLTessellationPartitionModeInteger;
        desc.vertexDescriptor = cVtxFmt.makeTessellationVertexLayout();

        MTLComputePipelineDescriptor* compDesc = [MTLComputePipelineDescriptor new];
        compDesc.computeFunction = compute.cast<MetalShaderStage>()->shader();
        compDesc.stageInputDescriptor = cVtxFmt.makeTessellationComputeLayout();

        NSError* err = nullptr;
        m_computeState = [ctx->m_dev newComputePipelineStateWithDescriptor:compDesc options:MTLPipelineOptionNone
                          reflection:nil error:&err];
        if (err)
            Log.report(logvisor::Fatal, "error making compute pipeline: %s",
                       [[err localizedDescription] UTF8String]);
    }

    void draw(MetalCommandQueue& q, size_t start, size_t count);
    void drawIndexed(MetalCommandQueue& q, size_t start, size_t count);
    void drawInstances(MetalCommandQueue& q, size_t start, size_t count, size_t instCount, size_t startInst);
    void drawInstancesIndexed(MetalCommandQueue& q, size_t start, size_t count, size_t instCount, size_t startInst);

public:
    id<MTLComputePipelineState> m_computeState;
    ~MetalTessellationShaderPipeline() = default;

};

static id<MTLBuffer> GetBufferGPUResource(const ObjToken<IGraphicsBuffer>& buf, int idx)
{
    if (buf->dynamic())
    {
        const MetalGraphicsBufferD<BaseGraphicsData>* cbuf = buf.cast<MetalGraphicsBufferD<BaseGraphicsData>>();
        return cbuf->m_bufs[idx];
    }
    else
    {
        const MetalGraphicsBufferS* cbuf = buf.cast<MetalGraphicsBufferS>();
        return cbuf->m_buf;
    }
}

static id<MTLTexture> GetTextureGPUResource(const ObjToken<ITexture>& tex, int idx, int bindIdx, bool depth)
{
    switch (tex->type())
    {
    case TextureType::Dynamic:
    {
        const MetalTextureD* ctex = tex.cast<MetalTextureD>();
        return ctex->m_texs[idx];
    }
    case TextureType::Static:
    {
        const MetalTextureS* ctex = tex.cast<MetalTextureS>();
        return ctex->m_tex;
    }
    case TextureType::StaticArray:
    {
        const MetalTextureSA* ctex = tex.cast<MetalTextureSA>();
        return ctex->m_tex;
    }
    case TextureType::Render:
    {
        const MetalTextureR* ctex = tex.cast<MetalTextureR>();
        return depth ? ctex->m_depthBindTex[bindIdx] : ctex->m_colorBindTex[bindIdx];
    }
    default: break;
    }
    return nullptr;
}

struct MetalShaderDataBinding : GraphicsDataNode<IShaderDataBinding>
{
    ObjToken<IShaderPipeline> m_pipeline;
    ObjToken<IGraphicsBuffer> m_vbuf;
    ObjToken<IGraphicsBuffer> m_instVbo;
    ObjToken<IGraphicsBuffer> m_ibuf;
    std::vector<ObjToken<IGraphicsBuffer>> m_ubufs;
    std::vector<size_t> m_ubufOffs;
    std::vector<bool> m_fubufs;
    struct BoundTex
    {
        ObjToken<ITexture> tex;
        int idx;
        bool depth;
    };
    std::vector<BoundTex> m_texs;
    size_t m_baseVert;
    size_t m_baseInst;

    MetalShaderDataBinding(const ObjToken<BaseGraphicsData>& d,
                           MetalContext* ctx,
                           const ObjToken<IShaderPipeline>& pipeline,
                           const ObjToken<IGraphicsBuffer>& vbuf,
                           const ObjToken<IGraphicsBuffer>& instVbo,
                           const ObjToken<IGraphicsBuffer>& ibuf,
                           size_t ubufCount, const ObjToken<IGraphicsBuffer>* ubufs, const PipelineStage* ubufStages,
                           const size_t* ubufOffs, const size_t* ubufSizes,
                           size_t texCount, const ObjToken<ITexture>* texs,
                           const int* texBindIdxs, const bool* depthBind,
                           size_t baseVert, size_t baseInst)
    : GraphicsDataNode<IShaderDataBinding>(d),
    m_pipeline(pipeline),
    m_vbuf(vbuf),
    m_instVbo(instVbo),
    m_ibuf(ibuf),
    m_baseVert(baseVert),
    m_baseInst(baseInst)
    {
        if (ubufCount && ubufStages)
        {
            m_fubufs.reserve(ubufCount);
            for (size_t i=0 ; i<ubufCount ; ++i)
                m_fubufs.push_back(ubufStages[i] == PipelineStage::Fragment);
        }

        if (ubufCount && ubufOffs && ubufSizes)
        {
            m_ubufOffs.reserve(ubufCount);
            for (size_t i=0 ; i<ubufCount ; ++i)
            {
#ifndef NDEBUG
                if (ubufOffs[i] % 256)
                    Log.report(logvisor::Fatal, "non-256-byte-aligned uniform-offset %d provided to newShaderDataBinding", int(i));
#endif
                m_ubufOffs.push_back(ubufOffs[i]);
            }
        }
        m_ubufs.reserve(ubufCount);
        for (size_t i=0 ; i<ubufCount ; ++i)
        {
#ifndef NDEBUG
            if (!ubufs[i])
                Log.report(logvisor::Fatal, "null uniform-buffer %d provided to newShaderDataBinding", int(i));
#endif
            m_ubufs.push_back(ubufs[i]);
        }
        m_texs.reserve(texCount);
        for (size_t i=0 ; i<texCount ; ++i)
        {
            m_texs.push_back({texs[i], texBindIdxs ? texBindIdxs[i] : 0, depthBind ? depthBind[i] : false});
        }
    }

    void bind(id<MTLRenderCommandEncoder> enc, int b)
    {
        m_pipeline.cast<MetalShaderPipeline>()->bind(enc);

        if (m_vbuf)
        {
            id<MTLBuffer> buf = GetBufferGPUResource(m_vbuf, b);
            [enc setVertexBuffer:buf offset:0 atIndex:0];
        }
        if (m_instVbo)
        {
            id<MTLBuffer> buf = GetBufferGPUResource(m_instVbo, b);
            [enc setVertexBuffer:buf offset:0 atIndex:1];
        }
        if (m_ubufOffs.size())
            for (size_t i=0 ; i<m_ubufs.size() ; ++i)
            {
                if (m_fubufs.size() && m_fubufs[i])
                    [enc setFragmentBuffer:GetBufferGPUResource(m_ubufs[i], b) offset:m_ubufOffs[i] atIndex:i+2];
                else
                    [enc setVertexBuffer:GetBufferGPUResource(m_ubufs[i], b) offset:m_ubufOffs[i] atIndex:i+2];
            }
        else
            for (size_t i=0 ; i<m_ubufs.size() ; ++i)
            {
                if (m_fubufs.size() && m_fubufs[i])
                    [enc setFragmentBuffer:GetBufferGPUResource(m_ubufs[i], b) offset:0 atIndex:i+2];
                else
                    [enc setVertexBuffer:GetBufferGPUResource(m_ubufs[i], b) offset:0 atIndex:i+2];
            }
        for (size_t i=0 ; i<m_texs.size() ; ++i)
            if (m_texs[i].tex)
            {
                [enc setFragmentTexture:GetTextureGPUResource(m_texs[i].tex, b, m_texs[i].idx,
                                                              m_texs[i].depth) atIndex:i];
                [enc setVertexTexture:GetTextureGPUResource(m_texs[i].tex, b, m_texs[i].idx,
                                                            m_texs[i].depth) atIndex:i];
            }
    }

    void bindCompute(id<MTLComputeCommandEncoder> enc, int b)
    {
        if (m_vbuf)
        {
            id<MTLBuffer> buf = GetBufferGPUResource(m_vbuf, b);
            [enc setBuffer:buf offset:0 atIndex:0];
        }
        if (m_instVbo)
        {
            id<MTLBuffer> buf = GetBufferGPUResource(m_instVbo, b);
            [enc setBuffer:buf offset:0 atIndex:1];
        }
    }
};

struct MetalCommandQueue : IGraphicsCommandQueue
{
    Platform platform() const { return IGraphicsDataFactory::Platform::Metal; }
    const char* platformName() const { return "Metal"; }
    MetalContext* m_ctx;
    IWindow* m_parentWindow;
    IGraphicsContext* m_parent;
    id<MTLCommandBuffer> m_cmdBuf;
    id<MTLRenderCommandEncoder> m_enc;
    id<MTLSamplerState> m_samplers[5];
    bool m_running = true;

    int m_fillBuf = 0;
    int m_drawBuf = 0;

    MetalCommandQueue(MetalContext* ctx, IWindow* parentWindow, IGraphicsContext* parent)
    : m_ctx(ctx), m_parentWindow(parentWindow), m_parent(parent)
    {
        @autoreleasepool
        {
            m_cmdBuf = [ctx->m_q commandBuffer];

            MTLSamplerDescriptor* sampDesc = [MTLSamplerDescriptor new];
            sampDesc.rAddressMode = MTLSamplerAddressModeRepeat;
            sampDesc.sAddressMode = MTLSamplerAddressModeRepeat;
            sampDesc.tAddressMode = MTLSamplerAddressModeRepeat;
            sampDesc.minFilter = MTLSamplerMinMagFilterLinear;
            sampDesc.magFilter = MTLSamplerMinMagFilterLinear;
            sampDesc.mipFilter = MTLSamplerMipFilterLinear;
            sampDesc.maxAnisotropy = ctx->m_anisotropy;
            sampDesc.borderColor = MTLSamplerBorderColorOpaqueWhite;
            m_samplers[0] = [ctx->m_dev newSamplerStateWithDescriptor:sampDesc];

            sampDesc.rAddressMode = MTLSamplerAddressModeClampToBorderColor;
            sampDesc.sAddressMode = MTLSamplerAddressModeClampToBorderColor;
            sampDesc.tAddressMode = MTLSamplerAddressModeClampToBorderColor;
            m_samplers[1] = [ctx->m_dev newSamplerStateWithDescriptor:sampDesc];

            sampDesc.rAddressMode = MTLSamplerAddressModeClampToBorderColor;
            sampDesc.sAddressMode = MTLSamplerAddressModeClampToBorderColor;
            sampDesc.tAddressMode = MTLSamplerAddressModeClampToBorderColor;
            sampDesc.borderColor = MTLSamplerBorderColorOpaqueBlack;
            m_samplers[2] = [ctx->m_dev newSamplerStateWithDescriptor:sampDesc];

            sampDesc.rAddressMode = MTLSamplerAddressModeClampToEdge;
            sampDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
            sampDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
            m_samplers[3] = [ctx->m_dev newSamplerStateWithDescriptor:sampDesc];

            sampDesc.rAddressMode = MTLSamplerAddressModeClampToEdge;
            sampDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
            sampDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
            sampDesc.minFilter = MTLSamplerMinMagFilterNearest;
            sampDesc.magFilter = MTLSamplerMinMagFilterNearest;
            m_samplers[4] = [ctx->m_dev newSamplerStateWithDescriptor:sampDesc];
        }
    }

    void startRenderer()
    {
        static_cast<MetalDataFactoryImpl*>(m_parent->getDataFactory())->SetupGammaResources();
    }

    void stopRenderer()
    {
        m_running = false;
        if (m_inProgress && m_cmdBuf.status != MTLCommandBufferStatusNotEnqueued)
            [m_cmdBuf waitUntilCompleted];
    }

    ~MetalCommandQueue()
    {
        if (m_running) stopRenderer();
    }

    MetalShaderDataBinding* m_boundData = nullptr;
    void setShaderDataBinding(const ObjToken<IShaderDataBinding>& binding)
    {
        @autoreleasepool
        {
            MetalShaderDataBinding* cbind = binding.cast<MetalShaderDataBinding>();
            cbind->bind(m_enc, m_fillBuf);
            m_boundData = cbind;
            [m_enc setFragmentSamplerStates:m_samplers withRange:NSMakeRange(0, 5)];
            [m_enc setVertexSamplerStates:m_samplers withRange:NSMakeRange(0, 5)];
        }
    }

    ObjToken<ITextureR> m_boundTarget;
    void _setRenderTarget(const ObjToken<ITextureR>& target, bool clearColor, bool clearDepth)
    {
        @autoreleasepool
        {
            MetalTextureR* ctarget = target.cast<MetalTextureR>();
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
            if (ctarget == m_boundTarget.get())
            {
                if (m_boundVp.width || m_boundVp.height)
                    [m_enc setViewport:m_boundVp];
                if (m_boundScissor.width || m_boundScissor.height)
                    [m_enc setScissorRect:m_boundScissor];
            }
            else
                m_boundTarget = target;
        }
    }

    void setRenderTarget(const ObjToken<ITextureR>& target)
    {
        _setRenderTarget(target, false, false);
    }

    MTLViewport m_boundVp = {};
    void setViewport(const SWindowRect& rect, float znear, float zfar)
    {
        m_boundVp = MTLViewport{double(rect.location[0]), double(rect.location[1]),
                                double(rect.size[0]), double(rect.size[1]), 1.f - zfar, 1.f - znear};
        [m_enc setViewport:m_boundVp];
    }

    MTLScissorRect m_boundScissor = {};
    void setScissor(const SWindowRect& rect)
    {
        if (m_boundTarget)
        {
            MetalTextureR* ctarget = m_boundTarget.cast<MetalTextureR>();
            SWindowRect intersectRect = rect.intersect(SWindowRect(0, 0, ctarget->m_width, ctarget->m_height));
            m_boundScissor = MTLScissorRect{NSUInteger(intersectRect.location[0]),
                NSUInteger(ctarget->m_height - intersectRect.location[1] - intersectRect.size[1]),
                NSUInteger(intersectRect.size[0]), NSUInteger(intersectRect.size[1])};
            [m_enc setScissorRect:m_boundScissor];
        }
    }

    std::unordered_map<MetalTextureR*, std::pair<size_t, size_t>> m_texResizes;
    void resizeRenderTexture(const ObjToken<ITextureR>& tex, size_t width, size_t height)
    {
        MetalTextureR* ctex = tex.cast<MetalTextureR>();
        m_texResizes[ctex] = std::make_pair(width, height);
    }

    void schedulePostFrameHandler(std::function<void(void)>&& func)
    {
        func();
    }

    void flushBufferUpdates() {}

    float m_clearColor[4] = {0.f,0.f,0.f,0.f};
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
        m_boundData->m_pipeline.cast<MetalShaderPipeline>()->draw(*this, start, count);
    }

    void drawIndexed(size_t start, size_t count)
    {
        m_boundData->m_pipeline.cast<MetalShaderPipeline>()->drawIndexed(*this, start, count);
    }

    void drawInstances(size_t start, size_t count, size_t instCount, size_t startInst)
    {
        m_boundData->m_pipeline.cast<MetalShaderPipeline>()->drawInstances(*this, start, count, instCount, startInst);
    }

    void drawInstancesIndexed(size_t start, size_t count, size_t instCount, size_t startInst)
    {
        m_boundData->m_pipeline.cast<MetalShaderPipeline>()->drawInstancesIndexed(*this, start, count, instCount, startInst);
    }

    void _resolveBindTexture(MetalTextureR* tex, const SWindowRect& rect, bool tlOrigin,
                             int bindIdx, bool color, bool depth)
    {
        if (tex->samples() > 1)
        {
            if (color && tex->m_colorBindTex[bindIdx])
            [[m_cmdBuf renderCommandEncoderWithDescriptor:tex->m_blitColor[bindIdx]] endEncoding];
            if (depth && tex->m_depthBindTex[bindIdx])
            [[m_cmdBuf renderCommandEncoderWithDescriptor:tex->m_blitDepth[bindIdx]] endEncoding];
        }
        else
        {
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
        }
    }

    void resolveBindTexture(const ObjToken<ITextureR>& texture, const SWindowRect& rect, bool tlOrigin,
                            int bindIdx, bool color, bool depth, bool clearDepth)
    {
        MetalTextureR* tex = texture.cast<MetalTextureR>();
        @autoreleasepool
        {
            [m_enc endEncoding];

            _resolveBindTexture(tex, rect, tlOrigin, bindIdx, color, depth);

            m_enc = [m_cmdBuf renderCommandEncoderWithDescriptor:clearDepth ? tex->m_clearDepthPassDesc : tex->m_passDesc];
            [m_enc setFrontFacingWinding:MTLWindingCounterClockwise];

            if (m_boundVp.width || m_boundVp.height)
                [m_enc setViewport:m_boundVp];
            if (m_boundScissor.width || m_boundScissor.height)
                [m_enc setScissorRect:m_boundScissor];
        }
    }

    ObjToken<ITextureR> m_needsDisplay;
    void resolveDisplay(const ObjToken<ITextureR>& source)
    {
        m_needsDisplay = source;
    }

    id<MTLBuffer> m_tessFactorBuffer = nullptr;
    id<MTLBuffer> ensureTessFactorBuffer(size_t patchCount)
    {
        size_t targetLength = sizeof(MTLQuadTessellationFactorsHalf) * patchCount;
        if (!m_tessFactorBuffer)
        {
            m_tessFactorBuffer = [m_ctx->m_dev newBufferWithLength:targetLength * 2 options:MTLResourceStorageModePrivate];
        }
        else if (m_tessFactorBuffer.length < targetLength)
        {
            targetLength *= 2;
            id<MTLBuffer> newBuf = [m_ctx->m_dev newBufferWithLength:targetLength options:MTLResourceStorageModePrivate];
            id<MTLBlitCommandEncoder> enc = [m_cmdBuf blitCommandEncoder];
            [enc copyFromBuffer:m_tessFactorBuffer sourceOffset:0 toBuffer:newBuf destinationOffset:0 size:m_tessFactorBuffer.length];
            [enc endEncoding];
            m_tessFactorBuffer = newBuf;
        }
        return m_tessFactorBuffer;
    }

    void dispatchTessKernel(id<MTLComputePipelineState> computeState, size_t patchStart,
                            size_t patchCount, uint32_t patchSize)
    {
        struct KernelPatchInfo
        {
            uint32_t numPatches; // total number of patches to process.
                                 // we need this because this value may
                                 // not be a multiple of threadgroup size.
            uint16_t numPatchesInThreadGroup; // number of patches processed by a
                                              // thread-group
            uint16_t numControlPointsPerPatch;
        } patchInfo = {uint32_t(patchCount), 32, uint16_t(patchSize)};

        [m_enc endEncoding];
        m_enc = nullptr;
        id<MTLBuffer> tessFactorBuf = ensureTessFactorBuffer(patchStart + patchCount);
        id<MTLComputeCommandEncoder> computeEnc = [m_cmdBuf computeCommandEncoder];
        [computeEnc setComputePipelineState:computeState];
        m_boundData->bindCompute(computeEnc, m_fillBuf);
        [computeEnc setStageInRegion:MTLRegionMake1D(patchStart, patchCount)];
        [computeEnc setBytes:&patchInfo length:sizeof(patchInfo) atIndex:2];
        [computeEnc setBuffer:tessFactorBuf
                       offset:patchStart * sizeof(MTLQuadTessellationFactorsHalf) atIndex:3];
        [computeEnc dispatchThreads:MTLSizeMake(patchCount, 1, 1) threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
        [computeEnc endEncoding];
        _setRenderTarget(m_boundTarget, false, false);
        m_boundData->bind(m_enc, m_fillBuf);
        [m_enc setFragmentSamplerStates:m_samplers withRange:NSMakeRange(0, 5)];
        [m_enc setVertexSamplerStates:m_samplers withRange:NSMakeRange(0, 5)];
        [m_enc setTessellationFactorBuffer:m_tessFactorBuffer offset:0 instanceStride:0];
    }

    bool m_inProgress = false;
    std::unordered_map<uintptr_t, MTLRenderPassDescriptor*> m_resolvePasses;
    std::unordered_map<uintptr_t, MTLRenderPassDescriptor*> m_gammaPasses;
    void execute()
    {
        if (!m_running)
            return;

        @autoreleasepool
        {
            /* Update dynamic data here */
            MetalDataFactoryImpl* gfxF = static_cast<MetalDataFactoryImpl*>(m_parent->getDataFactory());
            std::unique_lock<std::recursive_mutex> datalk(gfxF->m_dataMutex);
            if (gfxF->m_dataHead)
            {
                for (BaseGraphicsData& d : *gfxF->m_dataHead)
                {
                    if (d.m_DBufs)
                        for (IGraphicsBufferD& b : *d.m_DBufs)
                            static_cast<MetalGraphicsBufferD<BaseGraphicsData>&>(b).update(m_fillBuf);
                    if (d.m_DTexs)
                        for (ITextureD& t : *d.m_DTexs)
                            static_cast<MetalTextureD&>(t).update(m_fillBuf);
                }
            }
            if (gfxF->m_poolHead)
            {
                for (BaseGraphicsPool& p : *gfxF->m_poolHead)
                {
                    if (p.m_DBufs)
                        for (IGraphicsBufferD& b : *p.m_DBufs)
                            static_cast<MetalGraphicsBufferD<BaseGraphicsData>&>(b).update(m_fillBuf);
                }
            }
            datalk.unlock();

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
                        m_needsDisplay.reset();
                        return;
                    }
                }
                id<CAMetalDrawable> drawable = [w.m_metalLayer nextDrawable];
                if (drawable)
                {
                    MetalTextureR* src = m_needsDisplay.cast<MetalTextureR>();
                    id<MTLTexture> dest = drawable.texture;
                    if (src->m_colorTex.width == dest.width &&
                        src->m_colorTex.height == dest.height)
                    {
                        if (gfxF->m_gamma != 1.f)
                        {
                            SWindowRect rect(0, 0, src->m_width, src->m_height);
                            _resolveBindTexture(src, rect, true, 0, true, false);

                            uintptr_t key = uintptr_t(dest);
                            auto passSearch = m_gammaPasses.find(key);
                            if (passSearch == m_gammaPasses.end())
                            {
                                MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
                                desc.colorAttachments[0].texture = dest;
                                desc.colorAttachments[0].loadAction = MTLLoadActionLoad;
                                desc.colorAttachments[0].storeAction = MTLStoreActionStore;
                                passSearch = m_gammaPasses.insert(std::make_pair(key, desc)).first;
                            }

                            id<MTLRenderCommandEncoder> enc = [m_cmdBuf renderCommandEncoderWithDescriptor:passSearch->second];
                            MetalShaderDataBinding* gammaBinding = gfxF->m_gammaBinding.cast<MetalShaderDataBinding>();
                            gammaBinding->m_texs[0].tex = m_needsDisplay.get();
                            gammaBinding->bind(enc, m_drawBuf);
                            [enc setFragmentSamplerStates:m_samplers withRange:NSMakeRange(0, 5)];
                            [enc setVertexSamplerStates:m_samplers withRange:NSMakeRange(0, 5)];
                            [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
                            gammaBinding->m_texs[0].tex.reset();
                            [enc endEncoding];
                        }
                        else
                        {
                            if (src->samples() > 1)
                            {
                                uintptr_t key = uintptr_t(src->m_colorTex) ^ uintptr_t(dest);
                                auto passSearch = m_resolvePasses.find(key);
                                if (passSearch == m_resolvePasses.end())
                                {
                                    MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
                                    desc.colorAttachments[0].texture = src->m_colorTex;
                                    desc.colorAttachments[0].loadAction = MTLLoadActionLoad;
                                    desc.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
                                    desc.colorAttachments[0].resolveTexture = dest;
                                    passSearch = m_resolvePasses.insert(std::make_pair(key, desc)).first;
                                }
                                [[m_cmdBuf renderCommandEncoderWithDescriptor:passSearch->second] endEncoding];
                            }
                            else
                            {
                                id<MTLBlitCommandEncoder> blitEnc = [m_cmdBuf blitCommandEncoder];
                                [blitEnc copyFromTexture:src->m_colorTex
                                             sourceSlice:0
                                             sourceLevel:0
                                            sourceOrigin:MTLOriginMake(0, 0, 0)
                                              sourceSize:MTLSizeMake(dest.width, dest.height, 1)
                                               toTexture:dest
                                        destinationSlice:0
                                        destinationLevel:0
                                       destinationOrigin:MTLOriginMake(0, 0, 0)];
                                [blitEnc endEncoding];
                            }
                        }
                        [m_cmdBuf presentDrawable:drawable];
                    }
                }
                m_needsDisplay.reset();
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

void MetalShaderPipeline::draw(MetalCommandQueue& q, size_t start, size_t count)
{
    [q.m_enc drawPrimitives:m_drawPrim
                vertexStart:start + q.m_boundData->m_baseVert
                vertexCount:count];
}

void MetalShaderPipeline::drawIndexed(MetalCommandQueue& q, size_t start, size_t count)
{
    [q.m_enc drawIndexedPrimitives:m_drawPrim
                        indexCount:count
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:GetBufferGPUResource(q.m_boundData->m_ibuf, q.m_fillBuf)
                 indexBufferOffset:start*4
                     instanceCount:1
                        baseVertex:q.m_boundData->m_baseVert
                      baseInstance:0];
}

void MetalShaderPipeline::drawInstances(MetalCommandQueue& q, size_t start, size_t count, size_t instCount, size_t startInst)
{
    [q.m_enc drawPrimitives:m_drawPrim
                vertexStart:start + q.m_boundData->m_baseVert
                vertexCount:count
              instanceCount:instCount
               baseInstance:startInst + q.m_boundData->m_baseInst];
}

void MetalShaderPipeline::drawInstancesIndexed(MetalCommandQueue& q, size_t start, size_t count, size_t instCount, size_t startInst)
{
    [q.m_enc drawIndexedPrimitives:m_drawPrim
                        indexCount:count
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:GetBufferGPUResource(q.m_boundData->m_ibuf, q.m_fillBuf)
                 indexBufferOffset:start*4
                     instanceCount:instCount
                        baseVertex:q.m_boundData->m_baseVert
                      baseInstance:startInst + q.m_boundData->m_baseInst];
}

void MetalTessellationShaderPipeline::draw(MetalCommandQueue& q, size_t start, size_t count)
{
    q.dispatchTessKernel(m_computeState, start, count, m_patchSize);
    [q.m_enc drawPatches:m_patchSize
              patchStart:start
              patchCount:count
        patchIndexBuffer:nullptr
  patchIndexBufferOffset:0
           instanceCount:1
            baseInstance:0];
}

void MetalTessellationShaderPipeline::drawIndexed(MetalCommandQueue& q, size_t start, size_t count)
{
    q.dispatchTessKernel(m_computeState, start, count, m_patchSize);
    [q.m_enc drawIndexedPatches:m_patchSize
                     patchStart:0
                     patchCount:count
               patchIndexBuffer:nullptr
         patchIndexBufferOffset:0
        controlPointIndexBuffer:GetBufferGPUResource(q.m_boundData->m_ibuf, q.m_fillBuf)
  controlPointIndexBufferOffset:start*4
                  instanceCount:1
                   baseInstance:0];
}

void MetalTessellationShaderPipeline::drawInstances(MetalCommandQueue& q, size_t start, size_t count, size_t instCount, size_t startInst)
{
    q.dispatchTessKernel(m_computeState, start, count, m_patchSize);
    [q.m_enc drawPatches:m_patchSize
              patchStart:start
              patchCount:count
        patchIndexBuffer:nullptr
  patchIndexBufferOffset:0
           instanceCount:instCount
            baseInstance:startInst];
}

void MetalTessellationShaderPipeline::drawInstancesIndexed(MetalCommandQueue& q, size_t start, size_t count, size_t instCount, size_t startInst)
{
    q.dispatchTessKernel(m_computeState, start, count, m_patchSize);
    [q.m_enc drawIndexedPatches:m_patchSize
                     patchStart:0
                     patchCount:count
               patchIndexBuffer:nullptr
         patchIndexBufferOffset:0
        controlPointIndexBuffer:GetBufferGPUResource(q.m_boundData->m_ibuf, q.m_fillBuf)
  controlPointIndexBufferOffset:start*4
                  instanceCount:instCount
                   baseInstance:startInst];
}

MetalDataFactory::Context::Context(MetalDataFactory& parent __BooTraceArgs)
: m_parent(parent), m_data(new BaseGraphicsData(static_cast<MetalDataFactoryImpl&>(parent) __BooTraceArgsUse)) {}

MetalDataFactory::Context::~Context() {}

ObjToken<IGraphicsBufferS>
MetalDataFactory::Context::newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count)
{
    @autoreleasepool
    {
        MetalDataFactoryImpl& factory = static_cast<MetalDataFactoryImpl&>(m_parent);
        return {new MetalGraphicsBufferS(m_data, use, factory.m_ctx, data, stride, count)};
    }
}
ObjToken<IGraphicsBufferD>
MetalDataFactory::Context::newDynamicBuffer(BufferUse use, size_t stride, size_t count)
{
    @autoreleasepool
    {
        MetalDataFactoryImpl& factory = static_cast<MetalDataFactoryImpl&>(m_parent);
        MetalCommandQueue* q = static_cast<MetalCommandQueue*>(factory.m_parent->getCommandQueue());
        return {new MetalGraphicsBufferD<BaseGraphicsData>(m_data, q, use, factory.m_ctx, stride, count)};
    }
}

ObjToken<ITextureS>
MetalDataFactory::Context::newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                            TextureClampMode clampMode, const void* data, size_t sz)
{
    @autoreleasepool
    {
        MetalDataFactoryImpl& factory = static_cast<MetalDataFactoryImpl&>(m_parent);
        return {new MetalTextureS(m_data, factory.m_ctx, width, height, mips, fmt, data, sz)};
    }
}
ObjToken<ITextureSA>
MetalDataFactory::Context::newStaticArrayTexture(size_t width, size_t height, size_t layers, size_t mips,
                                                 TextureFormat fmt, TextureClampMode clampMode,
                                                 const void* data, size_t sz)
{
    @autoreleasepool
    {
        MetalDataFactoryImpl& factory = static_cast<MetalDataFactoryImpl&>(m_parent);
        return {new MetalTextureSA(m_data, factory.m_ctx, width, height, layers, mips, fmt, data, sz)};
    }
}
ObjToken<ITextureD>
MetalDataFactory::Context::newDynamicTexture(size_t width, size_t height, TextureFormat fmt,
                                             TextureClampMode clampMode)
{
    @autoreleasepool
    {
        MetalDataFactoryImpl& factory = static_cast<MetalDataFactoryImpl&>(m_parent);
        MetalCommandQueue* q = static_cast<MetalCommandQueue*>(factory.m_parent->getCommandQueue());
        return {new MetalTextureD(m_data, q, factory.m_ctx, width, height, fmt)};
    }
}
ObjToken<ITextureR>
MetalDataFactory::Context::newRenderTexture(size_t width, size_t height, TextureClampMode clampMode,
                                            size_t colorBindCount, size_t depthBindCount)
{
    @autoreleasepool
    {
        MetalDataFactoryImpl& factory = static_cast<MetalDataFactoryImpl&>(m_parent);
        return {new MetalTextureR(m_data, factory.m_ctx, width, height, factory.m_ctx->m_sampleCount,
                                  colorBindCount, depthBindCount)};
    }
}

ObjToken<IShaderStage>
MetalDataFactory::Context::newShaderStage(const uint8_t* data, size_t size, PipelineStage stage)
{
    @autoreleasepool
    {
        MetalDataFactoryImpl& factory = static_cast<MetalDataFactoryImpl&>(m_parent);
        return {new MetalShaderStage(m_data, factory.m_ctx, data, size, stage)};
    }
}

ObjToken<IShaderPipeline>
MetalDataFactory::Context::newShaderPipeline(ObjToken<IShaderStage> vertex, ObjToken<IShaderStage> fragment,
                                             ObjToken<IShaderStage> geometry, ObjToken<IShaderStage> control,
                                             ObjToken<IShaderStage> evaluation, const VertexFormatInfo& vtxFmt,
                                             const AdditionalPipelineInfo& additionalInfo)
{
    @autoreleasepool
    {
        MetalDataFactoryImpl& factory = static_cast<MetalDataFactoryImpl&>(m_parent);

        MetalShaderPipeline* ret;
        if (evaluation)
        {
            ret = new MetalTessellationShaderPipeline(m_data);
            ret->setup(factory.m_ctx, additionalInfo.depthAttachment ? factory.m_ctx->m_sampleCount : 1,
                       evaluation, fragment, control, vtxFmt, additionalInfo);
        }
        else
        {
            ret = new MetalShaderPipeline(m_data);
            ret->setup(factory.m_ctx, additionalInfo.depthAttachment ? factory.m_ctx->m_sampleCount : 1,
                       vertex, fragment, {}, vtxFmt, additionalInfo);
        }
        return {ret};
    }
}

ObjToken<IShaderDataBinding>
MetalDataFactory::Context::newShaderDataBinding(const ObjToken<IShaderPipeline>& pipeline,
                                                const ObjToken<IGraphicsBuffer>& vbo,
                                                const ObjToken<IGraphicsBuffer>& instVbo,
                                                const ObjToken<IGraphicsBuffer>& ibo,
                                                size_t ubufCount, const ObjToken<IGraphicsBuffer>* ubufs, const PipelineStage* ubufStages,
                                                const size_t* ubufOffs, const size_t* ubufSizes,
                                                size_t texCount, const ObjToken<ITexture>* texs,
                                                const int* texBindIdxs, const bool* depthBind,
                                                size_t baseVert, size_t baseInst)
{
    @autoreleasepool
    {
        MetalDataFactoryImpl& factory = static_cast<MetalDataFactoryImpl&>(m_parent);
        return {new MetalShaderDataBinding(m_data,
                                           factory.m_ctx, pipeline, vbo, instVbo, ibo,
                                           ubufCount, ubufs, ubufStages, ubufOffs,
                                           ubufSizes, texCount, texs, texBindIdxs,
                                           depthBind, baseVert, baseInst)};
    }
}

void MetalDataFactoryImpl::commitTransaction(const FactoryCommitFunc& trans __BooTraceArgs)
{
    MetalDataFactory::Context ctx(*this __BooTraceArgsUse);
    trans(ctx);
}

ObjToken<IGraphicsBufferD> MetalDataFactoryImpl::newPoolBuffer(BufferUse use, size_t stride, size_t count __BooTraceArgs)
{
    ObjToken<BaseGraphicsPool> pool(new BaseGraphicsPool(*this __BooTraceArgsUse));
    MetalCommandQueue* q = static_cast<MetalCommandQueue*>(m_parent->getCommandQueue());
    return {new MetalGraphicsBufferD<BaseGraphicsPool>(pool, q, use, m_ctx, stride, count)};
}

std::unique_ptr<IGraphicsCommandQueue> _NewMetalCommandQueue(MetalContext* ctx, IWindow* parentWindow,
                                                             IGraphicsContext* parent)
{
    return std::make_unique<MetalCommandQueue>(ctx, parentWindow, parent);
}

std::unique_ptr<IGraphicsDataFactory> _NewMetalDataFactory(IGraphicsContext* parent, MetalContext* ctx)
{
    return std::make_unique<MetalDataFactoryImpl>(parent, ctx);
}

std::vector<uint8_t> MetalDataFactory::CompileMetal(const char* source, PipelineStage stage)
{
    size_t strSz = strlen(source) + 1;
    std::vector<uint8_t> ret(strSz + 1);
    memcpy(ret.data() + 1, source, strSz);
    return ret;
}

}

#endif
