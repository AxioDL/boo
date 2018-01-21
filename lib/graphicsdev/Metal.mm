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
#include "xxhash.h"

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
"                      sampler clampSamp [[ sampler(2) ]],\n"
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

struct MetalShareableShader : IShareableShader<MetalDataFactoryImpl, MetalShareableShader>
{
    id<MTLFunction> m_shader;
    MetalShareableShader(MetalDataFactoryImpl& fac, uint64_t srcKey, uint64_t binKey, id<MTLFunction> s)
    : IShareableShader(fac, srcKey, binKey), m_shader(s) {}
};

class MetalDataFactoryImpl : public MetalDataFactory, public GraphicsDataFactoryHead
{
    friend struct MetalCommandQueue;
    friend class MetalDataFactory::Context;
    IGraphicsContext* m_parent;
    std::unordered_map<uint64_t, std::unique_ptr<MetalShareableShader>> m_sharedShaders;
    struct MetalContext* m_ctx;

    float m_gamma = 1.f;
    ObjToken<IShaderPipeline> m_gammaShader;
    ObjToken<ITextureD> m_gammaLUT;
    ObjToken<IGraphicsBufferS> m_gammaVBO;
    ObjToken<IVertexFormat> m_gammaVFMT;
    ObjToken<IShaderDataBinding> m_gammaBinding;
    void SetupGammaResources()
    {
        commitTransaction([this](IGraphicsDataFactory::Context& ctx)
        {
            const VertexElementDescriptor vfmt[] = {
                {nullptr, nullptr, VertexSemantic::Position4},
                {nullptr, nullptr, VertexSemantic::UV4}
            };
            m_gammaVFMT = ctx.newVertexFormat(2, vfmt);
            m_gammaShader = static_cast<Context&>(ctx).newShaderPipeline(GammaVS, GammaFS,
                nullptr, nullptr, m_gammaVFMT, BlendFactor::One, BlendFactor::Zero,
                Primitive::TriStrips, ZTest::None, false, true, false, CullMode::None);
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
            m_gammaBinding = ctx.newShaderDataBinding(m_gammaShader, m_gammaVFMT, m_gammaVBO.get(), {}, {},
                                                      0, nullptr, nullptr, 2, texs, nullptr, nullptr);
            return true;
        });
    }

public:
    std::unordered_map<uint64_t, uint64_t> m_sourceToBinary;
    char m_libfile[MAXPATHLEN];
    bool m_hasCompiler = false;

    MetalDataFactoryImpl(IGraphicsContext* parent, MetalContext* ctx)
    : m_parent(parent), m_ctx(ctx)
    {
        snprintf(m_libfile, MAXPATHLEN, "%sboo_metal_shader.metallib", getenv("TMPDIR"));
        for (auto& arg : APP->getArgs())
            if (arg == "--metal-compile")
            {
                m_hasCompiler = CheckForMetalCompiler();
                break;
            }
    }
    ~MetalDataFactoryImpl() = default;

    Platform platform() const { return Platform::Metal; }
    const char* platformName() const { return "Metal"; }
    void commitTransaction(const std::function<bool(IGraphicsDataFactory::Context& ctx)>&);
    ObjToken<IGraphicsBufferD> newPoolBuffer(BufferUse use, size_t stride, size_t count);
    void _unregisterShareableShader(uint64_t srcKey, uint64_t binKey) { m_sharedShaders.erase(srcKey); }

    static bool CheckForMetalCompiler()
    {
        pid_t pid = fork();
        if (!pid)
        {
            execlp("xcrun", "xcrun", "-sdk", "macosx", "metal", NULL);
            /* xcrun returns 72 if metal command not found;
             * emulate that if xcrun not found */
            exit(72);
        }

        int status, ret;
        while ((ret = waitpid(pid, &status, 0)) < 0 && errno == EINTR) {}
        if (ret < 0)
            return false;
        return WEXITSTATUS(status) == 1;
    }

    uint64_t CompileLib(std::vector<uint8_t>& blobOut, const char* source, uint64_t srcKey)
    {
        if (!m_hasCompiler)
        {
            /* Cache the source if there's no compiler */
            size_t sourceLen = strlen(source);

            /* First byte unset to indicate source data */
            blobOut.resize(sourceLen + 2);
            memcpy(&blobOut[1], source, sourceLen);
        }
        else
        {
            /* Cache the binary otherwise */
            int compilerOut[2];
            int compilerIn[2];
            pipe(compilerOut);
            pipe(compilerIn);

            /* Pipe source write to compiler */
            pid_t compilerPid = fork();
            if (!compilerPid)
            {
                dup2(compilerIn[0], STDIN_FILENO);
                dup2(compilerOut[1], STDOUT_FILENO);

                close(compilerOut[0]);
                close(compilerOut[1]);
                close(compilerIn[0]);
                close(compilerIn[1]);

                execlp("xcrun", "xcrun", "-sdk", "macosx", "metal", "-o", "/dev/stdout", "-Wno-unused-variable",
                       "-Wno-unused-const-variable", "-Wno-unused-function", "-x", "metal", "-", NULL);
                fprintf(stderr, "execlp fail %s\n", strerror(errno));
                exit(1);
            }
            close(compilerIn[0]);
            close(compilerOut[1]);

            /* Pipe compiler to linker */
            pid_t linkerPid = fork();
            if (!linkerPid)
            {
                dup2(compilerOut[0], STDIN_FILENO);

                close(compilerOut[0]);
                close(compilerIn[1]);

                /* metallib doesn't like outputting to a pipe, so temp file will have to do */
                execlp("xcrun", "xcrun", "-sdk", "macosx", "metallib", "-", "-o", m_libfile, NULL);
                fprintf(stderr, "execlp fail %s\n", strerror(errno));
                exit(1);
            }
            close(compilerOut[0]);

            /* Stream in source */
            const char* inPtr = source;
            size_t inRem = strlen(source);
            while (inRem)
            {
                ssize_t writeRes = write(compilerIn[1], inPtr, inRem);
                if (writeRes < 0)
                {
                    fprintf(stderr, "write fail %s\n", strerror(errno));
                    break;
                }
                inPtr += writeRes;
                inRem -= writeRes;
            }
            close(compilerIn[1]);

            /* Wait for completion */
            int compilerStat, linkerStat;
            if (waitpid(compilerPid, &compilerStat, 0) < 0 || waitpid(linkerPid, &linkerStat, 0) < 0)
            {
                fprintf(stderr, "waitpid fail %s\n", strerror(errno));
                return 0;
            }

            if (WEXITSTATUS(compilerStat) || WEXITSTATUS(linkerStat))
                return 0;

            /* Copy temp file into buffer with first byte set to indicate binary data */
            FILE* fin = fopen(m_libfile, "rb");
            fseek(fin, 0, SEEK_END);
            long libLen = ftell(fin);
            fseek(fin, 0, SEEK_SET);
            blobOut.resize(libLen + 1);
            blobOut[0] = 1;
            fread(&blobOut[1], 1, libLen, fin);
            fclose(fin);
        }

        XXH64_state_t hashState;
        XXH64_reset(&hashState, 0);
        XXH64_update(&hashState, blobOut.data(), blobOut.size());
        uint64_t binKey = XXH64_digest(&hashState);
        m_sourceToBinary[srcKey] = binKey;
        return binKey;
    }

    uint64_t CompileLib(__strong id<MTLLibrary>& libOut, const char* source, uint64_t srcKey,
                        MTLCompileOptions* compOpts, NSError * _Nullable *err)
    {
        libOut = [m_ctx->m_dev newLibraryWithSource:@(source)
                                            options:compOpts
                                              error:err];

        if (srcKey)
        {
            XXH64_state_t hashState;
            XXH64_reset(&hashState, 0);
            uint8_t zero = 0;
            XXH64_update(&hashState, &zero, 1);
            XXH64_update(&hashState, source, strlen(source) + 1);
            uint64_t binKey = XXH64_digest(&hashState);
            m_sourceToBinary[srcKey] = binKey;
            return binKey;
        }
        return 0;
    }

    void setDisplayGamma(float gamma)
    {
        if (m_ctx->m_pixelFormat == MTLPixelFormatRGBA16Float)
            m_gamma = gamma * 2.2f;
        else
            m_gamma = gamma;
        if (m_gamma != 1.f)
            UpdateGammaLUT(m_gammaLUT.get(), m_gamma);
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

struct MetalVertexFormat : GraphicsDataNode<IVertexFormat>
{
    size_t m_elementCount;
    MTLVertexDescriptor* m_vdesc;
    size_t m_stride = 0;
    size_t m_instStride = 0;
    MetalVertexFormat(const ObjToken<BaseGraphicsData>& parent,
                      size_t elementCount, const VertexElementDescriptor* elements)
    : GraphicsDataNode<IVertexFormat>(parent), m_elementCount(elementCount)
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

class MetalShaderPipeline : public GraphicsDataNode<IShaderPipeline>
{
    friend class MetalDataFactory;
    friend struct MetalCommandQueue;
    friend struct MetalShaderDataBinding;
    MTLCullMode m_cullMode = MTLCullModeNone;
    MTLPrimitiveType m_drawPrim;
    MetalShareableShader::Token m_vert;
    MetalShareableShader::Token m_frag;

    MetalShaderPipeline(const ObjToken<BaseGraphicsData>& parent,
                        MetalContext* ctx,
                        MetalShareableShader::Token&& vert,
                        MetalShareableShader::Token&& frag,
                        const ObjToken<IVertexFormat>& vtxFmt, NSUInteger targetSamples,
                        BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                        ZTest depthTest, bool depthWrite, bool colorWrite,
                        bool alphaWrite, CullMode culling)
    : GraphicsDataNode<IShaderPipeline>(parent),
      m_drawPrim(PRIMITIVE_TABLE[int(prim)]),
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
        desc.vertexDescriptor = vtxFmt.cast<MetalVertexFormat>()->m_vdesc;
        desc.sampleCount = targetSamples;
        desc.colorAttachments[0].pixelFormat = ctx->m_pixelFormat;
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
                [enc setFragmentTexture:GetTextureGPUResource(m_texs[i].tex, b, m_texs[i].idx, m_texs[i].depth) atIndex:i];
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
    id<MTLSamplerState> m_samplers[3];
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

            sampDesc.rAddressMode = MTLSamplerAddressModeClampToEdge;
            sampDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
            sampDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
            m_samplers[2] = [ctx->m_dev newSamplerStateWithDescriptor:sampDesc];
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
    MTLPrimitiveType m_currentPrimitive = MTLPrimitiveTypeTriangle;
    void setShaderDataBinding(const ObjToken<IShaderDataBinding>& binding)
    {
        @autoreleasepool
        {
            MetalShaderDataBinding* cbind = binding.cast<MetalShaderDataBinding>();
            cbind->bind(m_enc, m_fillBuf);
            m_boundData = cbind;
            m_currentPrimitive = cbind->m_pipeline.cast<MetalShaderPipeline>()->m_drawPrim;
            [m_enc setFragmentSamplerStates:m_samplers withRange:NSMakeRange(0, 3)];
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
        [m_enc drawPrimitives:m_currentPrimitive
                  vertexStart:start + m_boundData->m_baseVert
                  vertexCount:count];
    }

    void drawIndexed(size_t start, size_t count)
    {
        [m_enc drawIndexedPrimitives:m_currentPrimitive
                          indexCount:count
                           indexType:MTLIndexTypeUInt32
                         indexBuffer:GetBufferGPUResource(m_boundData->m_ibuf, m_fillBuf)
                   indexBufferOffset:start*4
                       instanceCount:1
                          baseVertex:m_boundData->m_baseVert
                        baseInstance:0];
    }

    void drawInstances(size_t start, size_t count, size_t instCount)
    {
        [m_enc drawPrimitives:m_currentPrimitive
                  vertexStart:start + m_boundData->m_baseVert
                  vertexCount:count
                instanceCount:instCount
                 baseInstance:m_boundData->m_baseInst];
    }

    void drawInstancesIndexed(size_t start, size_t count, size_t instCount)
    {
        [m_enc drawIndexedPrimitives:m_currentPrimitive
                          indexCount:count
                           indexType:MTLIndexTypeUInt32
                         indexBuffer:GetBufferGPUResource(m_boundData->m_ibuf, m_fillBuf)
                   indexBufferOffset:start*4
                       instanceCount:instCount
                          baseVertex:m_boundData->m_baseVert
                        baseInstance:m_boundData->m_baseInst];
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

MetalDataFactory::Context::Context(MetalDataFactory& parent)
: m_parent(parent), m_data(new BaseGraphicsData(static_cast<MetalDataFactoryImpl&>(parent))) {}

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

ObjToken<IVertexFormat>
MetalDataFactory::Context::newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements,
                                           size_t baseVert, size_t baseInst)
{
    @autoreleasepool
    {
        return {new struct MetalVertexFormat(m_data, elementCount, elements)};
    }
}

ObjToken<IShaderPipeline>
MetalDataFactory::Context::newShaderPipeline(const char* vertSource, const char* fragSource,
                                             std::vector<uint8_t>* vertBlobOut,
                                             std::vector<uint8_t>* fragBlobOut,
                                             const ObjToken<IVertexFormat>& vtxFmt,
                                             BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                                             ZTest depthTest, bool depthWrite, bool colorWrite,
                                             bool alphaWrite, CullMode culling)
{
    @autoreleasepool
    {
        MetalDataFactoryImpl& factory = static_cast<MetalDataFactoryImpl&>(m_parent);
        MTLCompileOptions* compOpts = [MTLCompileOptions new];
        compOpts.languageVersion = MTLLanguageVersion1_1;
        NSError* err = nullptr;

        XXH64_state_t hashState;
        uint64_t srcHashes[2] = {};
        uint64_t binHashes[2] = {};
        XXH64_reset(&hashState, 0);
        if (vertSource)
        {
            XXH64_update(&hashState, vertSource, strlen(vertSource));
            srcHashes[0] = XXH64_digest(&hashState);
            auto binSearch = factory.m_sourceToBinary.find(srcHashes[0]);
            if (binSearch != factory.m_sourceToBinary.cend())
                binHashes[0] = binSearch->second;
        }
        else if (vertBlobOut && !vertBlobOut->empty())
        {
            XXH64_update(&hashState, vertBlobOut->data(), vertBlobOut->size());
            binHashes[0] = XXH64_digest(&hashState);
        }
        XXH64_reset(&hashState, 0);
        if (fragSource)
        {
            XXH64_update(&hashState, fragSource, strlen(fragSource));
            srcHashes[1] = XXH64_digest(&hashState);
            auto binSearch = factory.m_sourceToBinary.find(srcHashes[1]);
            if (binSearch != factory.m_sourceToBinary.cend())
                binHashes[1] = binSearch->second;
        }
        else if (fragBlobOut && !fragBlobOut->empty())
        {
            XXH64_update(&hashState, fragBlobOut->data(), fragBlobOut->size());
            binHashes[1] = XXH64_digest(&hashState);
        }

        if (vertBlobOut && vertBlobOut->empty())
            binHashes[0] = factory.CompileLib(*vertBlobOut, vertSource, srcHashes[0]);

        if (fragBlobOut && fragBlobOut->empty())
            binHashes[1] = factory.CompileLib(*fragBlobOut, fragSource, srcHashes[1]);

        MetalShareableShader::Token vertShader;
        MetalShareableShader::Token fragShader;
        auto vertFind = binHashes[0] ? factory.m_sharedShaders.find(binHashes[0]) :
                        factory.m_sharedShaders.end();
        if (vertFind != factory.m_sharedShaders.end())
        {
            vertShader = vertFind->second->lock();
        }
        else
        {
            id<MTLLibrary> vertShaderLib;
            if (vertBlobOut && !vertBlobOut->empty())
            {
                if ((*vertBlobOut)[0] == 1)
                {
                    dispatch_data_t vertData = dispatch_data_create(vertBlobOut->data() + 1, vertBlobOut->size() - 1, nullptr, nullptr);
                    vertShaderLib = [factory.m_ctx->m_dev newLibraryWithData:vertData error:&err];
                    if (!vertShaderLib)
                        Log.report(logvisor::Fatal, "error loading vert library: %s", [[err localizedDescription] UTF8String]);
                }
                else
                {
                    factory.CompileLib(vertShaderLib, (char*)vertBlobOut->data() + 1, 0, compOpts, &err);
                }
            }
            else
                binHashes[0] = factory.CompileLib(vertShaderLib, vertSource, srcHashes[0], compOpts, &err);

            if (!vertShaderLib)
            {
                printf("%s\n", vertSource);
                Log.report(logvisor::Fatal, "error compiling vert shader: %s", [[err localizedDescription] UTF8String]);
            }
            id<MTLFunction> vertFunc = [vertShaderLib newFunctionWithName:@"vmain"];

            auto it =
                factory.m_sharedShaders.emplace(std::make_pair(binHashes[0],
                    std::make_unique<MetalShareableShader>(factory, srcHashes[0], binHashes[0], vertFunc))).first;
            vertShader = it->second->lock();
        }
        auto fragFind = binHashes[1] ? factory.m_sharedShaders.find(binHashes[1]) :
                        factory.m_sharedShaders.end();
        if (fragFind != factory.m_sharedShaders.end())
        {
            fragShader = fragFind->second->lock();
        }
        else
        {
            id<MTLLibrary> fragShaderLib;
            if (fragBlobOut && !fragBlobOut->empty())
            {
                if ((*fragBlobOut)[0] == 1)
                {
                    dispatch_data_t fragData = dispatch_data_create(fragBlobOut->data() + 1, fragBlobOut->size() - 1, nullptr, nullptr);
                    fragShaderLib = [factory.m_ctx->m_dev newLibraryWithData:fragData error:&err];
                    if (!fragShaderLib)
                        Log.report(logvisor::Fatal, "error loading frag library: %s", [[err localizedDescription] UTF8String]);
                }
                else
                {
                    factory.CompileLib(fragShaderLib, (char*)fragBlobOut->data() + 1, 0, compOpts, &err);
                }
            }
            else
                binHashes[1] = factory.CompileLib(fragShaderLib, fragSource, srcHashes[1], compOpts, &err);

            if (!fragShaderLib)
            {
                printf("%s\n", fragSource);
                Log.report(logvisor::Fatal, "error compiling frag shader: %s", [[err localizedDescription] UTF8String]);
            }
            id<MTLFunction> fragFunc = [fragShaderLib newFunctionWithName:@"fmain"];

            auto it =
                factory.m_sharedShaders.emplace(std::make_pair(binHashes[1],
                    std::make_unique<MetalShareableShader>(factory, srcHashes[1], binHashes[1], fragFunc))).first;
            fragShader = it->second->lock();
        }

        return {new MetalShaderPipeline(m_data, factory.m_ctx, std::move(vertShader), std::move(fragShader),
                                        vtxFmt, factory.m_ctx->m_sampleCount, srcFac, dstFac, prim, depthTest, depthWrite,
                                        colorWrite, alphaWrite, culling)};
    }
}

ObjToken<IShaderDataBinding>
MetalDataFactory::Context::newShaderDataBinding(const ObjToken<IShaderPipeline>& pipeline,
                                                const ObjToken<IVertexFormat>& vtxFormat,
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

void MetalDataFactoryImpl::commitTransaction(const FactoryCommitFunc& trans)
{
    MetalDataFactory::Context ctx(*this);
    trans(ctx);
}

ObjToken<IGraphicsBufferD> MetalDataFactoryImpl::newPoolBuffer(BufferUse use, size_t stride, size_t count)
{
    ObjToken<BaseGraphicsPool> pool(new BaseGraphicsPool(*this));
    MetalCommandQueue* q = static_cast<MetalCommandQueue*>(m_parent->getCommandQueue());
    return {new MetalGraphicsBufferD<BaseGraphicsPool>(pool, q, use, m_ctx, stride, count)};
}

IGraphicsCommandQueue* _NewMetalCommandQueue(MetalContext* ctx, IWindow* parentWindow,
                                             IGraphicsContext* parent)
{
    return new struct MetalCommandQueue(ctx, parentWindow, parent);
}

IGraphicsDataFactory* _NewMetalDataFactory(IGraphicsContext* parent, MetalContext* ctx)
{
    return new class MetalDataFactoryImpl(parent, ctx);
}

}

#endif
