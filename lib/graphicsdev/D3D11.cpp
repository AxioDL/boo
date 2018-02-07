#include "../win/Win32Common.hpp"
#include "logvisor/logvisor.hpp"
#include "boo/graphicsdev/D3D.hpp"
#include "boo/IGraphicsContext.hpp"
#include "Common.hpp"
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <d3dcompiler.h>
#include <comdef.h>
#include <algorithm>
#include <atomic>
#include <forward_list>
#include "xxhash.h"

#undef min
#undef max

extern pD3DCompile D3DCompilePROC;

static const char* GammaVS =
"struct VertData\n"
"{\n"
"    float4 posIn : POSITION;\n"
"    float4 uvIn : UV;\n"
"};\n"
"\n"
"struct VertToFrag\n"
"{\n"
"    float4 pos : SV_Position;\n"
"    float2 uv : UV;\n"
"};\n"
"\n"
"VertToFrag main(in VertData v)\n"
"{\n"
"    VertToFrag vtf;\n"
"    vtf.uv = v.uvIn.xy;\n"
"    vtf.pos = v.posIn;\n"
"    return vtf;\n"
"}\n";

static const char* GammaFS =
"struct VertToFrag\n"
"{\n"
"    float4 pos : SV_Position;\n"
"    float2 uv : UV;\n"
"};\n"
"\n"
"Texture2D screenTex : register(t0);\n"
"Texture2D gammaLUT : register(t1);\n"
"SamplerState samp : register(s2);\n"
"float4 main(in VertToFrag vtf) : SV_Target0\n"
"{\n"
"    int4 tex = int4(saturate(screenTex.Sample(samp, vtf.uv)) * float4(65535.0, 65535.0, 65535.0, 65535.0));\n"
"    float4 colorOut;\n"
"    for (int i=0 ; i<3 ; ++i)\n"
"        colorOut[i] = gammaLUT.Load(int3(tex[i] % 256, tex[i] / 256, 0)).r;\n"
"    return colorOut;\n"
"}\n";

namespace boo
{
static logvisor::Module Log("boo::D3D11");
class D3D11DataFactory;

struct D3D11ShareableShader : IShareableShader<D3D11DataFactory, D3D11ShareableShader>
{
    ComPtr<ID3D11DeviceChild> m_shader;
    ComPtr<ID3DBlob> m_vtxBlob;
    D3D11ShareableShader(D3D11DataFactory& fac, uint64_t srcKey, uint64_t binKey,
                         ComPtr<ID3D11DeviceChild>&& s, ComPtr<ID3DBlob>&& vb)
    : IShareableShader(fac, srcKey, binKey), m_shader(std::move(s)), m_vtxBlob(std::move(vb)) {}
    D3D11ShareableShader(D3D11DataFactory& fac, uint64_t srcKey, uint64_t binKey,
                         ComPtr<ID3D11DeviceChild>&& s)
    : IShareableShader(fac, srcKey, binKey), m_shader(std::move(s)) {}
};

static inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        // Set a breakpoint on this line to catch Win32 API errors.
#if !WINDOWS_STORE
        _com_error err(hr);
#else
        _com_error err(hr, L"D3D11 fail");
#endif
        LPCTSTR errMsg = err.ErrorMessage();
        Log.report(logvisor::Fatal, errMsg);
    }
}

static const D3D11_BIND_FLAG USE_TABLE[] =
{
    D3D11_BIND_VERTEX_BUFFER,
    D3D11_BIND_VERTEX_BUFFER,
    D3D11_BIND_INDEX_BUFFER,
    D3D11_BIND_CONSTANT_BUFFER
};

class D3D11GraphicsBufferS : public GraphicsDataNode<IGraphicsBufferS>
{
    friend class D3D11DataFactory;
    friend struct D3D11CommandQueue;

    size_t m_sz;
    D3D11GraphicsBufferS(const boo::ObjToken<BaseGraphicsData>& parent,
                         BufferUse use, D3D11Context* ctx,
                         const void* data, size_t stride, size_t count)
    : GraphicsDataNode<IGraphicsBufferS>(parent),
      m_stride(stride), m_count(count), m_sz(stride * count)
    {
        D3D11_SUBRESOURCE_DATA iData = {data};
        ThrowIfFailed(ctx->m_dev->CreateBuffer(&CD3D11_BUFFER_DESC(m_sz, USE_TABLE[int(use)], D3D11_USAGE_IMMUTABLE), &iData, &m_buf));
    }
public:
    size_t m_stride;
    size_t m_count;
    ComPtr<ID3D11Buffer> m_buf;
    ~D3D11GraphicsBufferS() = default;
};

template <class DataCls>
class D3D11GraphicsBufferD : public GraphicsDataNode<IGraphicsBufferD, DataCls>
{
    friend class D3D11DataFactory;
    friend struct D3D11CommandQueue;

    D3D11CommandQueue* m_q;
    std::unique_ptr<uint8_t[]> m_cpuBuf;
    size_t m_cpuSz;
    int m_validSlots = 0;
    D3D11GraphicsBufferD(const boo::ObjToken<DataCls>& parent,
                         D3D11CommandQueue* q, BufferUse use,
                         D3D11Context* ctx, size_t stride, size_t count)
    : GraphicsDataNode<IGraphicsBufferD, DataCls>(parent),
      m_q(q), m_stride(stride), m_count(count)
    {
        m_cpuSz = stride * count;
        m_cpuBuf.reset(new uint8_t[m_cpuSz]);
        for (int i=0 ; i<3 ; ++i)
            ThrowIfFailed(ctx->m_dev->CreateBuffer(&CD3D11_BUFFER_DESC(m_cpuSz, USE_TABLE[int(use)],
                          D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE), nullptr, &m_bufs[i]));
    }
    void update(ID3D11DeviceContext* ctx, int b);
public:
    size_t m_stride;
    size_t m_count;
    ComPtr<ID3D11Buffer> m_bufs[3];
    ~D3D11GraphicsBufferD() = default;

    void load(const void* data, size_t sz);
    void* map(size_t sz);
    void unmap();
};

class D3D11TextureS : public GraphicsDataNode<ITextureS>
{
    friend class D3D11DataFactory;
    size_t m_sz;

    D3D11TextureS(const boo::ObjToken<BaseGraphicsData>& parent,
                  D3D11Context* ctx, size_t width, size_t height, size_t mips,
                  TextureFormat fmt, const void* data, size_t sz)
    : GraphicsDataNode<ITextureS>(parent), m_sz(sz)
    {
        DXGI_FORMAT pfmt;
        int pxPitchNum = 1;
        int pxPitchDenom = 1;
        bool compressed = false;
        switch (fmt)
        {
        case TextureFormat::RGBA8:
            pfmt = DXGI_FORMAT_R8G8B8A8_UNORM;
            pxPitchNum = 4;
            break;
        case TextureFormat::I8:
            pfmt = DXGI_FORMAT_R8_UNORM;
            break;
        case TextureFormat::I16:
            pfmt = DXGI_FORMAT_R16_UNORM;
            pxPitchNum = 2;
            break;
        case TextureFormat::DXT1:
            pfmt = DXGI_FORMAT_BC1_UNORM;
            compressed = true;
            pxPitchNum = 1;
            pxPitchDenom = 2;
            break;
        default:
            Log.report(logvisor::Fatal, "unsupported tex format");
        }

        CD3D11_TEXTURE2D_DESC desc(pfmt, width, height, 1, mips,
            D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_IMMUTABLE);

        const uint8_t* dataIt = static_cast<const uint8_t*>(data);
        D3D11_SUBRESOURCE_DATA upData[16] = {};
        for (size_t i=0 ; i<mips && i<16 ; ++i)
        {
            upData[i].pSysMem = dataIt;
            upData[i].SysMemPitch = width * pxPitchNum / pxPitchDenom;
            upData[i].SysMemSlicePitch = upData[i].SysMemPitch * height;
            if (compressed)
                upData[i].SysMemPitch = width * 2;
            dataIt += upData[i].SysMemSlicePitch;
            if (width > 1)
                width /= 2;
            if (height > 1)
                height /= 2;
        }

        ThrowIfFailed(ctx->m_dev->CreateTexture2D(&desc, upData, &m_tex));
        ThrowIfFailed(ctx->m_dev->CreateShaderResourceView(m_tex.Get(),
            &CD3D11_SHADER_RESOURCE_VIEW_DESC(m_tex.Get(), D3D_SRV_DIMENSION_TEXTURE2D, pfmt, 0, mips), &m_srv));
    }
public:
    ComPtr<ID3D11Texture2D> m_tex;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    ~D3D11TextureS() = default;
};

class D3D11TextureSA : public GraphicsDataNode<ITextureSA>
{
    friend class D3D11DataFactory;

    size_t m_sz;
    D3D11TextureSA(const boo::ObjToken<BaseGraphicsData>& parent,
                   D3D11Context* ctx, size_t width, size_t height, size_t layers,
                   size_t mips, TextureFormat fmt, const void* data, size_t sz)
    : GraphicsDataNode<ITextureSA>(parent), m_sz(sz)
    {
        size_t pixelPitch;
        DXGI_FORMAT pixelFmt;
        switch (fmt)
        {
        case TextureFormat::RGBA8:
            pixelPitch = 4;
            pixelFmt = DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
        case TextureFormat::I8:
            pixelPitch = 1;
            pixelFmt = DXGI_FORMAT_R8_UNORM;
            break;
        case TextureFormat::I16:
            pixelPitch = 2;
            pixelFmt = DXGI_FORMAT_R16_UNORM;
            break;
        }

        CD3D11_TEXTURE2D_DESC desc(pixelFmt, width, height, layers, mips,
                                   D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_IMMUTABLE);

        const uint8_t* dataIt = static_cast<const uint8_t*>(data);
        std::unique_ptr<D3D11_SUBRESOURCE_DATA[]> upData(new D3D11_SUBRESOURCE_DATA[layers * mips]);
        D3D11_SUBRESOURCE_DATA* outIt = upData.get();
        for (size_t i=0 ; i<mips ; ++i)
        {
            for (size_t j=0 ; j<layers ; ++j)
            {
                outIt->pSysMem = dataIt;
                outIt->SysMemPitch = width * pixelPitch;
                outIt->SysMemSlicePitch = outIt->SysMemPitch * height;
                dataIt += outIt->SysMemSlicePitch;
                ++outIt;
            }
            if (width > 1)
                width /= 2;
            if (height > 1)
                height /= 2;
        }
        ThrowIfFailed(ctx->m_dev->CreateTexture2D(&desc, upData.get(), &m_tex));

        ThrowIfFailed(ctx->m_dev->CreateShaderResourceView(m_tex.Get(),
            &CD3D11_SHADER_RESOURCE_VIEW_DESC(m_tex.Get(), D3D_SRV_DIMENSION_TEXTURE2DARRAY, pixelFmt), &m_srv));
    }
public:
    ComPtr<ID3D11Texture2D> m_tex;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    ~D3D11TextureSA() = default;
};

class D3D11TextureD : public GraphicsDataNode<ITextureD>
{
    friend class D3D11DataFactory;
    friend struct D3D11CommandQueue;

    size_t m_width = 0;
    size_t m_height = 0;
    D3D11CommandQueue* m_q;
    std::unique_ptr<uint8_t[]> m_cpuBuf;
    size_t m_cpuSz;
    size_t m_pxPitch;
    int m_validSlots = 0;
    D3D11TextureD(const boo::ObjToken<BaseGraphicsData>& parent,
                  D3D11CommandQueue* q, D3D11Context* ctx,
                  size_t width, size_t height, TextureFormat fmt)
    : GraphicsDataNode<ITextureD>(parent), m_width(width), m_height(height), m_q(q)
    {
        DXGI_FORMAT pixelFmt;
        switch (fmt)
        {
        case TextureFormat::RGBA8:
            pixelFmt = DXGI_FORMAT_R8G8B8A8_UNORM;
            m_pxPitch = 4;
            break;
        case TextureFormat::I8:
            pixelFmt = DXGI_FORMAT_R8_UNORM;
            m_pxPitch = 1;
            break;
        case TextureFormat::I16:
            pixelFmt = DXGI_FORMAT_R16_UNORM;
            m_pxPitch = 2;
            break;
        default:
            Log.report(logvisor::Fatal, "unsupported tex format");
        }

        m_cpuSz = width * height * m_pxPitch;
        m_cpuBuf.reset(new uint8_t[m_cpuSz]);

        CD3D11_TEXTURE2D_DESC desc(pixelFmt, width, height, 1, 1,
            D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
        for (int i=0 ; i<3 ; ++i)
        {
            ThrowIfFailed(ctx->m_dev->CreateTexture2D(&desc, nullptr, &m_texs[i]));
            ThrowIfFailed(ctx->m_dev->CreateShaderResourceView(m_texs[i].Get(),
                &CD3D11_SHADER_RESOURCE_VIEW_DESC(m_texs[i].Get(), D3D_SRV_DIMENSION_TEXTURE2D, pixelFmt), &m_srvs[i]));
        }
    }
    void update(ID3D11DeviceContext* ctx, int b);
public:
    ComPtr<ID3D11Texture2D> m_texs[3];
    ComPtr<ID3D11ShaderResourceView> m_srvs[3];
    ~D3D11TextureD() = default;

    void load(const void* data, size_t sz);
    void* map(size_t sz);
    void unmap();
};

#define MAX_BIND_TEXS 4

class D3D11TextureR : public GraphicsDataNode<ITextureR>
{
    friend class D3D11DataFactory;
    friend struct D3D11CommandQueue;

    size_t m_width = 0;
    size_t m_height = 0;
    size_t m_samples = 0;
    size_t m_colorBindCount;
    size_t m_depthBindCount;

    void Setup(D3D11Context* ctx)
    {
        ThrowIfFailed(ctx->m_dev->CreateTexture2D(&CD3D11_TEXTURE2D_DESC(ctx->m_fbFormat, m_width, m_height,
            1, 1, D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0, m_samples), nullptr, &m_colorTex));
        ThrowIfFailed(ctx->m_dev->CreateTexture2D(&CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_D24_UNORM_S8_UINT, m_width, m_height,
            1, 1, D3D11_BIND_DEPTH_STENCIL, D3D11_USAGE_DEFAULT, 0, m_samples), nullptr, &m_depthTex));

        D3D11_RTV_DIMENSION rtvDim;
        D3D11_DSV_DIMENSION dsvDim;

        if (m_samples > 1)
        {
            rtvDim = D3D11_RTV_DIMENSION_TEXTURE2DMS;
            dsvDim = D3D11_DSV_DIMENSION_TEXTURE2DMS;
        }
        else
        {
            rtvDim = D3D11_RTV_DIMENSION_TEXTURE2D;
            dsvDim = D3D11_DSV_DIMENSION_TEXTURE2D;
        }

        ThrowIfFailed(ctx->m_dev->CreateRenderTargetView(m_colorTex.Get(),
            &CD3D11_RENDER_TARGET_VIEW_DESC(m_colorTex.Get(), rtvDim), &m_rtv));
        ThrowIfFailed(ctx->m_dev->CreateDepthStencilView(m_depthTex.Get(),
            &CD3D11_DEPTH_STENCIL_VIEW_DESC(m_depthTex.Get(), dsvDim), &m_dsv));

        for (size_t i=0 ; i<m_colorBindCount ; ++i)
        {
            ThrowIfFailed(ctx->m_dev->CreateTexture2D(&CD3D11_TEXTURE2D_DESC(ctx->m_fbFormat, m_width, m_height,
                1, 1, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, 1), nullptr, &m_colorBindTex[i]));
            ThrowIfFailed(ctx->m_dev->CreateShaderResourceView(m_colorBindTex[i].Get(),
                &CD3D11_SHADER_RESOURCE_VIEW_DESC(m_colorBindTex[i].Get(), D3D11_SRV_DIMENSION_TEXTURE2D), &m_colorSrv[i]));
        }

        for (size_t i=0 ; i<m_depthBindCount ; ++i)
        {
            ThrowIfFailed(ctx->m_dev->CreateTexture2D(&CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R24G8_TYPELESS, m_width, m_height,
                1, 1, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, 1), nullptr, &m_depthBindTex[i]));
            ThrowIfFailed(ctx->m_dev->CreateShaderResourceView(m_depthBindTex[i].Get(),
                &CD3D11_SHADER_RESOURCE_VIEW_DESC(m_depthBindTex[i].Get(), D3D11_SRV_DIMENSION_TEXTURE2D,
                                                  DXGI_FORMAT_R24_UNORM_X8_TYPELESS), &m_depthSrv[i]));
        }
    }

    D3D11TextureR(const boo::ObjToken<BaseGraphicsData>& parent,
                  D3D11Context* ctx, size_t width, size_t height, size_t samples,
                  size_t colorBindCount, size_t depthBindCount)
    : GraphicsDataNode<ITextureR>(parent), m_width(width), m_height(height), m_samples(samples),
      m_colorBindCount(colorBindCount), m_depthBindCount(depthBindCount)
    {
        if (colorBindCount > MAX_BIND_TEXS)
            Log.report(logvisor::Fatal, "too many color bindings for render texture");
        if (depthBindCount > MAX_BIND_TEXS)
            Log.report(logvisor::Fatal, "too many depth bindings for render texture");

        if (samples == 0) m_samples = 1;
        Setup(ctx);
    }
public:
    size_t samples() const {return m_samples;}
    ComPtr<ID3D11Texture2D> m_colorTex;
    ComPtr<ID3D11RenderTargetView> m_rtv;

    ComPtr<ID3D11Texture2D> m_depthTex;
    ComPtr<ID3D11DepthStencilView> m_dsv;

    ComPtr<ID3D11Texture2D> m_colorBindTex[MAX_BIND_TEXS];
    ComPtr<ID3D11ShaderResourceView> m_colorSrv[MAX_BIND_TEXS];

    ComPtr<ID3D11Texture2D> m_depthBindTex[MAX_BIND_TEXS];
    ComPtr<ID3D11ShaderResourceView> m_depthSrv[MAX_BIND_TEXS];

    ~D3D11TextureR() = default;

    void resize(D3D11Context* ctx, size_t width, size_t height)
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

static const char* SEMANTIC_NAME_TABLE[] =
{
    nullptr,
    "POSITION",
    "POSITION",
    "NORMAL",
    "NORMAL",
    "COLOR",
    "COLOR",
    "UV",
    "UV",
    "WEIGHT",
    "MODELVIEW"
};

static const DXGI_FORMAT SEMANTIC_TYPE_TABLE[] =
{
    DXGI_FORMAT_UNKNOWN,
    DXGI_FORMAT_R32G32B32_FLOAT,
    DXGI_FORMAT_R32G32B32A32_FLOAT,
    DXGI_FORMAT_R32G32B32_FLOAT,
    DXGI_FORMAT_R32G32B32A32_FLOAT,
    DXGI_FORMAT_R32G32B32A32_FLOAT,
    DXGI_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT_R32G32_FLOAT,
    DXGI_FORMAT_R32G32B32A32_FLOAT,
    DXGI_FORMAT_R32G32B32A32_FLOAT,
    DXGI_FORMAT_R32G32B32A32_FLOAT
};

struct D3D11VertexFormat : GraphicsDataNode<IVertexFormat>
{
    size_t m_elementCount;
    std::unique_ptr<D3D11_INPUT_ELEMENT_DESC[]> m_elements;
    size_t m_stride = 0;
    size_t m_instStride = 0;

    D3D11VertexFormat(const boo::ObjToken<BaseGraphicsData>& parent,
                      size_t elementCount, const VertexElementDescriptor* elements)
    : GraphicsDataNode<IVertexFormat>(parent), m_elementCount(elementCount),
      m_elements(new D3D11_INPUT_ELEMENT_DESC[elementCount])
    {
        memset(m_elements.get(), 0, elementCount * sizeof(D3D11_INPUT_ELEMENT_DESC));
        for (size_t i=0 ; i<elementCount ; ++i)
        {
            const VertexElementDescriptor* elemin = &elements[i];
            D3D11_INPUT_ELEMENT_DESC& elem = m_elements[i];
            int semantic = int(elemin->semantic & boo::VertexSemantic::SemanticMask);
            elem.SemanticName = SEMANTIC_NAME_TABLE[semantic];
            elem.SemanticIndex = elemin->semanticIdx;
            elem.Format = SEMANTIC_TYPE_TABLE[semantic];
            if ((elemin->semantic & boo::VertexSemantic::Instanced) != boo::VertexSemantic::None)
            {
                elem.InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
                elem.InstanceDataStepRate = 1;
                elem.InputSlot = 1;
                elem.AlignedByteOffset = m_instStride;
                m_instStride += SEMANTIC_SIZE_TABLE[semantic];
            }
            else
            {
                elem.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
                elem.AlignedByteOffset = m_stride;
                m_stride += SEMANTIC_SIZE_TABLE[semantic];
            }
        }
    }
};

static const D3D11_PRIMITIVE_TOPOLOGY PRIMITIVE_TABLE[] =
{
    D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
    D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
};

static const D3D11_BLEND BLEND_FACTOR_TABLE[] =
{
    D3D11_BLEND_ZERO,
    D3D11_BLEND_ONE,
    D3D11_BLEND_SRC_COLOR,
    D3D11_BLEND_INV_SRC_COLOR,
    D3D11_BLEND_DEST_COLOR,
    D3D11_BLEND_INV_DEST_COLOR,
    D3D11_BLEND_SRC_ALPHA,
    D3D11_BLEND_INV_SRC_ALPHA,
    D3D11_BLEND_DEST_ALPHA,
    D3D11_BLEND_INV_DEST_ALPHA,
    D3D11_BLEND_SRC1_COLOR,
    D3D11_BLEND_INV_SRC1_COLOR
};

class D3D11ShaderPipeline : public GraphicsDataNode<IShaderPipeline>
{
    friend class D3D11DataFactory;
    friend struct D3D11ShaderDataBinding;
    boo::ObjToken<IVertexFormat> m_vtxFmt;
    D3D11ShareableShader::Token m_vert;
    D3D11ShareableShader::Token m_pixel;

    D3D11ShaderPipeline(const boo::ObjToken<BaseGraphicsData>& parent,
                        D3D11Context* ctx,
                        D3D11ShareableShader::Token&& vert,
                        D3D11ShareableShader::Token&& pixel,
                        const boo::ObjToken<IVertexFormat>& vtxFmt,
                        BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                        ZTest depthTest, bool depthWrite, bool colorWrite,
                        bool alphaWrite, bool overwriteAlpha, CullMode culling)
    : GraphicsDataNode<IShaderPipeline>(parent), m_vtxFmt(vtxFmt),
      m_vert(std::move(vert)), m_pixel(std::move(pixel)),
      m_topology(PRIMITIVE_TABLE[int(prim)])
    {
        m_vert.get().m_shader.As<ID3D11VertexShader>(&m_vShader);
        m_pixel.get().m_shader.As<ID3D11PixelShader>(&m_pShader);

        D3D11_CULL_MODE cullMode;
        switch (culling)
        {
        case CullMode::None:
        default:
            cullMode = D3D11_CULL_NONE;
            break;
        case CullMode::Backface:
            cullMode = D3D11_CULL_BACK;
            break;
        case CullMode::Frontface:
            cullMode = D3D11_CULL_FRONT;
            break;
        }

        CD3D11_RASTERIZER_DESC rasDesc(D3D11_FILL_SOLID, cullMode, true,
            D3D11_DEFAULT_DEPTH_BIAS, D3D11_DEFAULT_DEPTH_BIAS_CLAMP,
            D3D11_DEFAULT_SLOPE_SCALED_DEPTH_BIAS, true, true, false, false);
        ThrowIfFailed(ctx->m_dev->CreateRasterizerState(&rasDesc, &m_rasState));

        CD3D11_DEPTH_STENCIL_DESC dsDesc(D3D11_DEFAULT);
        dsDesc.DepthEnable = depthTest != ZTest::None;
        dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK(depthWrite);
        switch (depthTest)
        {
        case ZTest::None:
        default:
            dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
            break;
        case ZTest::LEqual:
            dsDesc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
            break;
        case ZTest::Greater:
            dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
            break;
        case ZTest::GEqual:
            dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
            break;
        case ZTest::Equal:
            dsDesc.DepthFunc = D3D11_COMPARISON_EQUAL;
            break;
        }
        ThrowIfFailed(ctx->m_dev->CreateDepthStencilState(&dsDesc, &m_dsState));

        CD3D11_BLEND_DESC blDesc(D3D11_DEFAULT);
        blDesc.RenderTarget[0].BlendEnable = (dstFac != BlendFactor::Zero);
        if (srcFac == BlendFactor::Subtract || dstFac == BlendFactor::Subtract)
        {
            blDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            blDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
            blDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;
            if (overwriteAlpha)
            {
                blDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
                blDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
                blDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            }
            else
            {
                blDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
                blDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
                blDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_REV_SUBTRACT;
            }
        }
        else
        {
            blDesc.RenderTarget[0].SrcBlend = BLEND_FACTOR_TABLE[int(srcFac)];
            blDesc.RenderTarget[0].DestBlend = BLEND_FACTOR_TABLE[int(dstFac)];
            blDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            if (overwriteAlpha)
            {
                blDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
                blDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
            }
            else
            {
                blDesc.RenderTarget[0].SrcBlendAlpha = BLEND_FACTOR_TABLE[int(srcFac)];
                blDesc.RenderTarget[0].DestBlendAlpha = BLEND_FACTOR_TABLE[int(dstFac)];
            }
            blDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        }
        blDesc.RenderTarget[0].RenderTargetWriteMask =
                (colorWrite ? (D3D11_COLOR_WRITE_ENABLE_RED |
                               D3D11_COLOR_WRITE_ENABLE_GREEN |
                               D3D11_COLOR_WRITE_ENABLE_BLUE) : 0) |
                (alphaWrite ? D3D11_COLOR_WRITE_ENABLE_ALPHA : 0);
        ThrowIfFailed(ctx->m_dev->CreateBlendState(&blDesc, &m_blState));

        const auto& vertBuf = m_vert.get().m_vtxBlob;
        D3D11VertexFormat* cvtxFmt = vtxFmt.cast<D3D11VertexFormat>();
        ThrowIfFailed(ctx->m_dev->CreateInputLayout(cvtxFmt->m_elements.get(), cvtxFmt->m_elementCount,
            vertBuf->GetBufferPointer(), vertBuf->GetBufferSize(), &m_inLayout));
    }
public:
    ComPtr<ID3D11VertexShader> m_vShader;
    ComPtr<ID3D11PixelShader> m_pShader;
    ComPtr<ID3D11RasterizerState> m_rasState;
    ComPtr<ID3D11DepthStencilState> m_dsState;
    ComPtr<ID3D11BlendState> m_blState;
    ComPtr<ID3D11InputLayout> m_inLayout;
    D3D11_PRIMITIVE_TOPOLOGY m_topology;
    ~D3D11ShaderPipeline() = default;
    D3D11ShaderPipeline& operator=(const D3D11ShaderPipeline&) = delete;
    D3D11ShaderPipeline(const D3D11ShaderPipeline&) = delete;

    void bind(ID3D11DeviceContext* ctx)
    {
        ctx->VSSetShader(m_vShader.Get(), nullptr, 0);
        ctx->PSSetShader(m_pShader.Get(), nullptr, 0);
        ctx->RSSetState(m_rasState.Get());
        ctx->OMSetDepthStencilState(m_dsState.Get(), 0);
        ctx->OMSetBlendState(m_blState.Get(), nullptr, 0xffffffff);
        ctx->IASetInputLayout(m_inLayout.Get());
        ctx->IASetPrimitiveTopology(m_topology);
    }
};

struct D3D11ShaderDataBinding : public GraphicsDataNode<IShaderDataBinding>
{
    boo::ObjToken<IShaderPipeline> m_pipeline;
    boo::ObjToken<IGraphicsBuffer> m_vbuf;
    boo::ObjToken<IGraphicsBuffer> m_instVbuf;
    boo::ObjToken<IGraphicsBuffer> m_ibuf;
    std::vector<boo::ObjToken<IGraphicsBuffer>> m_ubufs;
    std::unique_ptr<UINT[]> m_ubufFirstConsts;
    std::unique_ptr<UINT[]> m_ubufNumConsts;
    std::unique_ptr<bool[]> m_pubufs;
    struct BoundTex
    {
        boo::ObjToken<ITexture> tex;
        int idx;
        bool depth;
    };
    std::vector<BoundTex> m_texs;
    UINT m_baseOffsets[2];

    D3D11ShaderDataBinding(const boo::ObjToken<BaseGraphicsData>& d,
                           D3D11Context* ctx,
                           const boo::ObjToken<IShaderPipeline>& pipeline,
                           const boo::ObjToken<IGraphicsBuffer>& vbuf,
                           const boo::ObjToken<IGraphicsBuffer>& instVbuf,
                           const boo::ObjToken<IGraphicsBuffer>& ibuf,
                           size_t ubufCount, const boo::ObjToken<IGraphicsBuffer>* ubufs, const PipelineStage* ubufStages,
                           const size_t* ubufOffs, const size_t* ubufSizes,
                           size_t texCount, const boo::ObjToken<ITexture>* texs,
                           const int* texBindIdx, const bool* depthBind,
                           size_t baseVert, size_t baseInst)
    : GraphicsDataNode<IShaderDataBinding>(d),
      m_pipeline(pipeline),
      m_vbuf(vbuf),
      m_instVbuf(instVbuf),
      m_ibuf(ibuf)
    {
        m_ubufs.reserve(ubufCount);
        m_texs.reserve(texCount);

        D3D11ShaderPipeline* cpipeline = m_pipeline.cast<D3D11ShaderPipeline>();
        D3D11VertexFormat* vtxFmt = cpipeline->m_vtxFmt.cast<D3D11VertexFormat>();
        m_baseOffsets[0] = UINT(baseVert * vtxFmt->m_stride);
        m_baseOffsets[1] = UINT(baseInst * vtxFmt->m_instStride);

        if (ubufStages)
        {
            m_pubufs.reset(new bool[ubufCount]);
            for (size_t i=0 ; i<ubufCount ; ++i)
                 m_pubufs[i] = ubufStages[i] == PipelineStage::Fragment;
        }
        if (ubufOffs && ubufSizes)
        {
            m_ubufFirstConsts.reset(new UINT[ubufCount]);
            m_ubufNumConsts.reset(new UINT[ubufCount]);
            for (size_t i=0 ; i<ubufCount ; ++i)
            {
#ifndef NDEBUG
                if (ubufOffs[i] % 256)
                    Log.report(logvisor::Fatal, "non-256-byte-aligned uniform-offset %d provided to newShaderDataBinding", int(i));
#endif
                m_ubufFirstConsts[i] = ubufOffs[i] / 16;
                m_ubufNumConsts[i] = ((ubufSizes[i] + 255) & ~255) / 16;
            }
        }
        for (size_t i=0 ; i<ubufCount ; ++i)
        {
#ifndef NDEBUG
            if (!ubufs[i])
                Log.report(logvisor::Fatal, "null uniform-buffer %d provided to newShaderDataBinding", int(i));
#endif
            m_ubufs.push_back(ubufs[i]);
        }
        for (size_t i=0 ; i<texCount ; ++i)
        {
            m_texs.push_back({texs[i], texBindIdx ? texBindIdx[i] : 0, depthBind ? depthBind[i] : false});
        }
    }

    void bind(ID3D11DeviceContext1* ctx, int b)
    {
        m_pipeline.cast<D3D11ShaderPipeline>()->bind(ctx);

        ID3D11Buffer* bufs[2] = {};
        UINT strides[2] = {};

        if (m_vbuf)
        {
            if (m_vbuf->dynamic())
            {
                D3D11GraphicsBufferD<BaseGraphicsData>* cbuf =
                        m_vbuf.cast<D3D11GraphicsBufferD<BaseGraphicsData>>();
                bufs[0] = cbuf->m_bufs[b].Get();
                strides[0] = UINT(cbuf->m_stride);
            }
            else
            {
                D3D11GraphicsBufferS* cbuf = m_vbuf.cast<D3D11GraphicsBufferS>();
                bufs[0] = cbuf->m_buf.Get();
                strides[0] = UINT(cbuf->m_stride);
            }
        }

        if (m_instVbuf)
        {
            if (m_instVbuf->dynamic())
            {
                D3D11GraphicsBufferD<BaseGraphicsData>* cbuf =
                        m_instVbuf.cast<D3D11GraphicsBufferD<BaseGraphicsData>>();
                bufs[1] = cbuf->m_bufs[b].Get();
                strides[1] = UINT(cbuf->m_stride);
            }
            else
            {
                D3D11GraphicsBufferS* cbuf = m_instVbuf.cast<D3D11GraphicsBufferS>();
                bufs[1] = cbuf->m_buf.Get();
                strides[1] = UINT(cbuf->m_stride);
            }
        }

        ctx->IASetVertexBuffers(0, 2, bufs, strides, m_baseOffsets);

        if (m_ibuf)
        {
            if (m_ibuf->dynamic())
            {
                D3D11GraphicsBufferD<BaseGraphicsData>* cbuf =
                        m_ibuf.cast<D3D11GraphicsBufferD<BaseGraphicsData>>();
                ctx->IASetIndexBuffer(cbuf->m_bufs[b].Get(), DXGI_FORMAT_R32_UINT, 0);
            }
            else
            {
                D3D11GraphicsBufferS* cbuf = m_ibuf.cast<D3D11GraphicsBufferS>();
                ctx->IASetIndexBuffer(cbuf->m_buf.Get(), DXGI_FORMAT_R32_UINT, 0);
            }
        }

        if (m_ubufs.size())
        {
            if (m_ubufFirstConsts)
            {
                ID3D11Buffer* constBufs[8] = {};
                ctx->VSSetConstantBuffers(0, m_ubufs.size(), constBufs);
                for (int i=0 ; i<8 && i<m_ubufs.size() ; ++i)
                {
                    if (m_pubufs && m_pubufs[i])
                        continue;
                    if (m_ubufs[i]->dynamic())
                    {
                        D3D11GraphicsBufferD<BaseGraphicsData>* cbuf =
                                m_ubufs[i].cast<D3D11GraphicsBufferD<BaseGraphicsData>>();
                        constBufs[i] = cbuf->m_bufs[b].Get();
                    }
                    else
                    {
                        D3D11GraphicsBufferS* cbuf = m_ubufs[i].cast<D3D11GraphicsBufferS>();
                        constBufs[i] = cbuf->m_buf.Get();
                    }
                }
                ctx->VSSetConstantBuffers1(0, m_ubufs.size(), constBufs, m_ubufFirstConsts.get(), m_ubufNumConsts.get());

                if (m_pubufs)
                {
                    ID3D11Buffer* constBufs[8] = {};
                    ctx->PSSetConstantBuffers(0, m_ubufs.size(), constBufs);
                    for (int i=0 ; i<8 && i<m_ubufs.size() ; ++i)
                    {
                        if (!m_pubufs[i])
                            continue;
                        if (m_ubufs[i]->dynamic())
                        {
                            D3D11GraphicsBufferD<BaseGraphicsData>* cbuf =
                                    m_ubufs[i].cast<D3D11GraphicsBufferD<BaseGraphicsData>>();
                            constBufs[i] = cbuf->m_bufs[b].Get();
                        }
                        else
                        {
                            D3D11GraphicsBufferS* cbuf = m_ubufs[i].cast<D3D11GraphicsBufferS>();
                            constBufs[i] = cbuf->m_buf.Get();
                        }
                    }
                    ctx->PSSetConstantBuffers1(0, m_ubufs.size(), constBufs, m_ubufFirstConsts.get(), m_ubufNumConsts.get());
                }
            }
            else
            {
                ID3D11Buffer* constBufs[8] = {};
                for (int i=0 ; i<8 && i<m_ubufs.size() ; ++i)
                {
                    if (m_pubufs && m_pubufs[i])
                        continue;
                    if (m_ubufs[i]->dynamic())
                    {
                        D3D11GraphicsBufferD<BaseGraphicsData>* cbuf =
                                m_ubufs[i].cast<D3D11GraphicsBufferD<BaseGraphicsData>>();
                        constBufs[i] = cbuf->m_bufs[b].Get();
                    }
                    else
                    {
                        D3D11GraphicsBufferS* cbuf = m_ubufs[i].cast<D3D11GraphicsBufferS>();
                        constBufs[i] = cbuf->m_buf.Get();
                    }
                }
                ctx->VSSetConstantBuffers(0, m_ubufs.size(), constBufs);

                if (m_pubufs)
                {
                    ID3D11Buffer* constBufs[8] = {};
                    for (int i=0 ; i<8 && i<m_ubufs.size() ; ++i)
                    {
                        if (!m_pubufs[i])
                            continue;
                        if (m_ubufs[i]->dynamic())
                        {
                            D3D11GraphicsBufferD<BaseGraphicsData>* cbuf =
                                    m_ubufs[i].cast<D3D11GraphicsBufferD<BaseGraphicsData>>();
                            constBufs[i] = cbuf->m_bufs[b].Get();
                        }
                        else
                        {
                            D3D11GraphicsBufferS* cbuf = m_ubufs[i].cast<D3D11GraphicsBufferS>();
                            constBufs[i] = cbuf->m_buf.Get();
                        }
                    }
                    ctx->PSSetConstantBuffers(0, m_ubufs.size(), constBufs);
                }
            }
        }

        if (m_texs.size())
        {
            ID3D11ShaderResourceView* srvs[8] = {};
            for (int i=0 ; i<8 && i<m_texs.size() ; ++i)
            {
                if (m_texs[i].tex)
                {
                    switch (m_texs[i].tex->type())
                    {
                    case TextureType::Dynamic:
                    {
                        D3D11TextureD* ctex = m_texs[i].tex.cast<D3D11TextureD>();
                        srvs[i] = ctex->m_srvs[b].Get();
                        break;
                    }
                    case TextureType::Static:
                    {
                        D3D11TextureS* ctex = m_texs[i].tex.cast<D3D11TextureS>();
                        srvs[i] = ctex->m_srv.Get();
                        break;
                    }
                    case TextureType::StaticArray:
                    {
                        D3D11TextureSA* ctex = m_texs[i].tex.cast<D3D11TextureSA>();
                        srvs[i] = ctex->m_srv.Get();
                        break;
                    }
                    case TextureType::Render:
                    {
                        D3D11TextureR* ctex = m_texs[i].tex.cast<D3D11TextureR>();
                        srvs[i] = m_texs[i].depth ? ctex->m_depthSrv[m_texs[i].idx].Get() :
                                                    ctex->m_colorSrv[m_texs[i].idx].Get();
                        break;
                    }
                    }
                }
            }
            ctx->PSSetShaderResources(0, m_texs.size(), srvs);
        }
    }
};

struct D3D11CommandQueue : IGraphicsCommandQueue
{
    Platform platform() const {return IGraphicsDataFactory::Platform::D3D11;}
    const SystemChar* platformName() const {return _S("D3D11");}
    D3D11Context* m_ctx;
    D3D11Context::Window* m_windowCtx;
    IGraphicsContext* m_parent;
    ComPtr<ID3D11DeviceContext1> m_deferredCtx;

    size_t m_fillBuf = 0;
    size_t m_completeBuf = 0;
    size_t m_drawBuf = 0;
    bool m_running = true;

    std::mutex m_mt;
    std::condition_variable m_cv;
    std::mutex m_initmt;
    std::condition_variable m_initcv;
    std::unique_lock<std::mutex> m_initlk;
    std::thread m_thr;

    struct CommandList
    {
        ComPtr<ID3D11CommandList> list;
        std::vector<boo::ObjToken<boo::IObj>> resTokens;
        boo::ObjToken<ITextureR> workDoPresent;

        void reset()
        {
            list.Reset();
            resTokens.clear();
            workDoPresent.reset();
        }
    };
    CommandList m_cmdLists[3];

    std::recursive_mutex m_dynamicLock;
    void ProcessDynamicLoads(ID3D11DeviceContext* ctx);
    static void RenderingWorker(D3D11CommandQueue* self);

    D3D11CommandQueue(D3D11Context* ctx, D3D11Context::Window* windowCtx, IGraphicsContext* parent)
    : m_ctx(ctx), m_windowCtx(windowCtx), m_parent(parent),
      m_initlk(m_initmt),
      m_thr(RenderingWorker, this)
    {
        m_initcv.wait(m_initlk);
        m_initlk.unlock();
        ThrowIfFailed(ctx->m_dev->CreateDeferredContext1(0, &m_deferredCtx));
    }

    void startRenderer();

    void stopRenderer()
    {
        m_running = false;
        m_cv.notify_one();
        m_thr.join();
    }

    ~D3D11CommandQueue()
    {
        if (m_running) stopRenderer();
    }

    void setShaderDataBinding(const boo::ObjToken<IShaderDataBinding>& binding)
    {
        D3D11ShaderDataBinding* cbind = binding.cast<D3D11ShaderDataBinding>();
        cbind->bind(m_deferredCtx.Get(), m_fillBuf);
        m_cmdLists[m_fillBuf].resTokens.push_back(binding.get());

        ID3D11SamplerState* samp[] = {m_ctx->m_ss[0].Get(),
                                      m_ctx->m_ss[1].Get(),
                                      m_ctx->m_ss[2].Get(),
                                      m_ctx->m_ss[3].Get()};
        m_deferredCtx->PSSetSamplers(0, 4, samp);
    }

    boo::ObjToken<ITextureR> m_boundTarget;
    void setRenderTarget(const boo::ObjToken<ITextureR>& target)
    {
        D3D11TextureR* ctarget = target.cast<D3D11TextureR>();
        ID3D11RenderTargetView* view[] = {ctarget->m_rtv.Get()};
        m_deferredCtx->OMSetRenderTargets(1, view, ctarget->m_dsv.Get());
        m_boundTarget = target;
    }

    void setViewport(const SWindowRect& rect, float znear, float zfar)
    {
        if (m_boundTarget)
        {
            D3D11TextureR* ctarget = m_boundTarget.cast<D3D11TextureR>();
            int boundHeight = ctarget->m_height;
            D3D11_VIEWPORT vp = {FLOAT(rect.location[0]), FLOAT(boundHeight - rect.location[1] - rect.size[1]),
                                 FLOAT(rect.size[0]), FLOAT(rect.size[1]), 1.f - zfar, 1.f - znear};
            m_deferredCtx->RSSetViewports(1, &vp);
        }
    }

    void setScissor(const SWindowRect& rect)
    {
        if (m_boundTarget)
        {
            D3D11TextureR* ctarget = m_boundTarget.cast<D3D11TextureR>();
            int boundHeight = ctarget->m_height;
            D3D11_RECT d3drect = {LONG(rect.location[0]), LONG(boundHeight - rect.location[1] - rect.size[1]),
                                  LONG(rect.location[0] + rect.size[0]), LONG(boundHeight - rect.location[1])};
            m_deferredCtx->RSSetScissorRects(1, &d3drect);
        }
    }

    std::unordered_map<D3D11TextureR*, std::pair<size_t, size_t>> m_texResizes;
    void resizeRenderTexture(const boo::ObjToken<ITextureR>& tex, size_t width, size_t height)
    {
        D3D11TextureR* ctex = tex.cast<D3D11TextureR>();
        std::unique_lock<std::mutex> lk(m_mt);
        m_texResizes[ctex] = std::make_pair(width, height);
    }

    void schedulePostFrameHandler(std::function<void(void)>&& func)
    {
        func();
    }

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
        D3D11TextureR* ctarget = m_boundTarget.cast<D3D11TextureR>();
        if (render)
            m_deferredCtx->ClearRenderTargetView(ctarget->m_rtv.Get(), m_clearColor);
        if (depth)
            m_deferredCtx->ClearDepthStencilView(ctarget->m_dsv.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
    }

    void draw(size_t start, size_t count)
    {
        m_deferredCtx->Draw(count, start);
    }

    void drawIndexed(size_t start, size_t count)
    {
        m_deferredCtx->DrawIndexed(count, start, 0);
    }

    void drawInstances(size_t start, size_t count, size_t instCount)
    {
        m_deferredCtx->DrawInstanced(count, instCount, start, 0);
    }

    void drawInstancesIndexed(size_t start, size_t count, size_t instCount)
    {
        m_deferredCtx->DrawIndexedInstanced(count, instCount, start, 0, 0);
    }

    void _resolveBindTexture(ID3D11DeviceContext1* ctx, const D3D11TextureR* tex, const SWindowRect& rect,
                             bool tlOrigin, int bindIdx, bool color, bool depth)
    {
        if (color && tex->m_colorBindCount)
        {
            if (tex->m_samples > 1)
            {
                ctx->ResolveSubresource(tex->m_colorBindTex[bindIdx].Get(), 0, tex->m_colorTex.Get(), 0,
                                        m_ctx->m_fbFormat);
            }
            else
            {
                SWindowRect intersectRect = rect.intersect(SWindowRect(0, 0, tex->m_width, tex->m_height));
                int y = tlOrigin ? intersectRect.location[1] : (tex->m_height - intersectRect.size[1] - intersectRect.location[1]);
                D3D11_BOX box = {UINT(intersectRect.location[0]), UINT(y), 0,
                                 UINT(intersectRect.location[0] + intersectRect.size[0]), UINT(y + intersectRect.size[1]), 1};
                ctx->CopySubresourceRegion1(tex->m_colorBindTex[bindIdx].Get(), 0, box.left, box.top, 0,
                                            tex->m_colorTex.Get(), 0, &box, D3D11_COPY_DISCARD);
            }
        }
        if (depth && tex->m_depthBindCount)
        {
            if (tex->m_samples > 1)
            {
                ctx->ResolveSubresource(tex->m_depthBindTex[bindIdx].Get(), 0, tex->m_depthTex.Get(), 0,
                                                  DXGI_FORMAT_D24_UNORM_S8_UINT);
            }
            else
            {
                ctx->CopyResource(tex->m_depthBindTex[bindIdx].Get(), tex->m_depthTex.Get());
            }
        }
    }

    void resolveBindTexture(const boo::ObjToken<ITextureR>& texture, const SWindowRect& rect,
                            bool tlOrigin, int bindIdx, bool color, bool depth, bool clearDepth)
    {
        const D3D11TextureR* tex = texture.cast<D3D11TextureR>();
        _resolveBindTexture(m_deferredCtx.Get(), tex, rect, tlOrigin, bindIdx, color, depth);
        if (clearDepth)
            m_deferredCtx->ClearDepthStencilView(tex->m_dsv.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
    }

    boo::ObjToken<ITextureR> m_doPresent;
    void resolveDisplay(const boo::ObjToken<ITextureR>& source)
    {
        m_doPresent = source;
    }

    void execute();
};

template <class DataCls>
void D3D11GraphicsBufferD<DataCls>::update(ID3D11DeviceContext* ctx, int b)
{
    int slot = 1 << b;
    if ((slot & m_validSlots) == 0)
    {
        ID3D11Buffer* res = m_bufs[b].Get();
        D3D11_MAPPED_SUBRESOURCE d;
        if (SUCCEEDED(ctx->Map(res, 0, D3D11_MAP_WRITE_DISCARD, 0, &d)))
        {
            memcpy(d.pData, m_cpuBuf.get(), m_cpuSz);
            ctx->Unmap(res, 0);
        }
        m_validSlots |= slot;
    }
}
template <class DataCls>
void D3D11GraphicsBufferD<DataCls>::load(const void* data, size_t sz)
{
    std::unique_lock<std::recursive_mutex> lk(m_q->m_dynamicLock);
    size_t bufSz = std::min(sz, m_cpuSz);
    memcpy(m_cpuBuf.get(), data, bufSz);
    m_validSlots = 0;
}
template <class DataCls>
void* D3D11GraphicsBufferD<DataCls>::map(size_t sz)
{
    if (sz > m_cpuSz)
        return nullptr;
    m_q->m_dynamicLock.lock();
    return m_cpuBuf.get();
}
template <class DataCls>
void D3D11GraphicsBufferD<DataCls>::unmap()
{
    m_validSlots = 0;
    m_q->m_dynamicLock.unlock();
}

void D3D11TextureD::update(ID3D11DeviceContext* ctx, int b)
{
    int slot = 1 << b;
    if ((slot & m_validSlots) == 0)
    {
        ID3D11Texture2D* res = m_texs[b].Get();
        D3D11_MAPPED_SUBRESOURCE d;
        ctx->Map(res, 0, D3D11_MAP_WRITE_DISCARD, 0, &d);
        size_t rowSz = m_pxPitch * m_width;
        for (size_t i=0 ; i<m_cpuSz ; i+=rowSz, reinterpret_cast<uint8_t*&>(d.pData)+=d.RowPitch)
            memmove(d.pData, m_cpuBuf.get() + i, rowSz);
        ctx->Unmap(res, 0);
        m_validSlots |= slot;
    }
}
void D3D11TextureD::load(const void* data, size_t sz)
{
    std::unique_lock<std::recursive_mutex> lk(m_q->m_dynamicLock);
    size_t bufSz = std::min(sz, m_cpuSz);
    memcpy(m_cpuBuf.get(), data, bufSz);
    m_validSlots = 0;
}
void* D3D11TextureD::map(size_t sz)
{
    if (sz > m_cpuSz)
        return nullptr;
    m_q->m_dynamicLock.lock();
    return m_cpuBuf.get();
}
void D3D11TextureD::unmap()
{
    m_validSlots = 0;
    m_q->m_dynamicLock.unlock();
}

class D3D11DataFactory : public ID3DDataFactory, public GraphicsDataFactoryHead
{
    friend struct D3D11CommandQueue;
    IGraphicsContext* m_parent;
    struct D3D11Context* m_ctx;
    std::unordered_map<uint64_t, std::unique_ptr<D3D11ShareableShader>> m_sharedShaders;
    std::unordered_map<uint64_t, uint64_t> m_sourceToBinary;

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
                nullptr, nullptr, nullptr, m_gammaVFMT, BlendFactor::One, BlendFactor::Zero,
                Primitive::TriStrips, ZTest::None, false, true, false, CullMode::None, true);
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
    D3D11DataFactory(IGraphicsContext* parent, D3D11Context* ctx)
    : m_parent(parent), m_ctx(ctx)
    {
        UINT qLevels;
        while (SUCCEEDED(ctx->m_dev->CheckMultisampleQualityLevels
                         (m_ctx->m_fbFormat, m_ctx->m_sampleCount, &qLevels)) && !qLevels)
            m_ctx->m_sampleCount = flp2(m_ctx->m_sampleCount - 1);
    }

    Platform platform() const {return Platform::D3D11;}
    const SystemChar* platformName() const {return _S("D3D11");}

    class Context : public ID3DDataFactory::Context
    {
        friend class D3D11DataFactory;
        D3D11DataFactory& m_parent;
        boo::ObjToken<BaseGraphicsData> m_data;
        Context(D3D11DataFactory& parent)
        : m_parent(parent), m_data(new BaseGraphicsData(parent)) {}
    public:
        Platform platform() const {return Platform::D3D11;}
        const SystemChar* platformName() const {return _S("D3D11");}

        boo::ObjToken<IGraphicsBufferS> newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count)
        {
            return {new D3D11GraphicsBufferS(m_data, use, m_parent.m_ctx, data, stride, count)};
        }

        boo::ObjToken<IGraphicsBufferD> newDynamicBuffer(BufferUse use, size_t stride, size_t count)
        {
            D3D11CommandQueue* q = static_cast<D3D11CommandQueue*>(m_parent.m_parent->getCommandQueue());
            return {new D3D11GraphicsBufferD<BaseGraphicsData>(m_data, q, use, m_parent.m_ctx, stride, count)};
        }

        boo::ObjToken<ITextureS> newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
            TextureClampMode clampMode, const void* data, size_t sz)
        {
            return {new D3D11TextureS(m_data, m_parent.m_ctx, width, height, mips, fmt, data, sz)};
        }

        boo::ObjToken<ITextureSA> newStaticArrayTexture(size_t width, size_t height, size_t layers, size_t mips,
                                          TextureFormat fmt, TextureClampMode clampMode, const void* data, size_t sz)
        {
            return {new D3D11TextureSA(m_data, m_parent.m_ctx, width, height, layers, mips, fmt, data, sz)};
        }

        boo::ObjToken<ITextureD> newDynamicTexture(size_t width, size_t height, TextureFormat fmt, TextureClampMode clampMode)
        {
            D3D11CommandQueue* q = static_cast<D3D11CommandQueue*>(m_parent.m_parent->getCommandQueue());
            return {new D3D11TextureD(m_data, q, m_parent.m_ctx, width, height, fmt)};
        }

        boo::ObjToken<ITextureR> newRenderTexture(size_t width, size_t height, TextureClampMode clampMode,
                                    size_t colorBindCount, size_t depthBindCount)
        {
            return {new D3D11TextureR(m_data, m_parent.m_ctx, width, height, m_parent.m_ctx->m_sampleCount,
                                      colorBindCount, depthBindCount)};
        }

        boo::ObjToken<IVertexFormat> newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements,
                                       size_t baseVert, size_t baseInst)
        {
            return {new struct D3D11VertexFormat(m_data, elementCount, elements)};
        }

#if _DEBUG && 0
#define BOO_D3DCOMPILE_FLAG D3DCOMPILE_DEBUG | D3DCOMPILE_OPTIMIZATION_LEVEL0
#else
#define BOO_D3DCOMPILE_FLAG D3DCOMPILE_OPTIMIZATION_LEVEL3
#endif

        static uint64_t CompileVert(ComPtr<ID3DBlob>& vertBlobOut, const char* vertSource, uint64_t srcKey,
                                    D3D11DataFactory& factory)
        {
            ComPtr<ID3DBlob> errBlob;
            if (FAILED(D3DCompilePROC(vertSource, strlen(vertSource), "HECL Vert Source", nullptr, nullptr, "main",
                "vs_5_0", BOO_D3DCOMPILE_FLAG, 0, &vertBlobOut, &errBlob)))
            {
                printf("%s\n", vertSource);
                Log.report(logvisor::Fatal, "error compiling vert shader: %s", errBlob->GetBufferPointer());
            }

            XXH64_state_t hashState;
            XXH64_reset(&hashState, 0);
            XXH64_update(&hashState, vertBlobOut->GetBufferPointer(), vertBlobOut->GetBufferSize());
            uint64_t binKey = XXH64_digest(&hashState);
            factory.m_sourceToBinary[srcKey] = binKey;
            return binKey;
        }

        static uint64_t CompileFrag(ComPtr<ID3DBlob>& fragBlobOut, const char* fragSource, uint64_t srcKey,
                                    D3D11DataFactory& factory)
        {
            ComPtr<ID3DBlob> errBlob;
            if (FAILED(D3DCompilePROC(fragSource, strlen(fragSource), "HECL Pixel Source", nullptr, nullptr, "main",
                "ps_5_0", BOO_D3DCOMPILE_FLAG, 0, &fragBlobOut, &errBlob)))
            {
                printf("%s\n", fragSource);
                Log.report(logvisor::Fatal, "error compiling pixel shader: %s", errBlob->GetBufferPointer());
            }

            XXH64_state_t hashState;
            XXH64_reset(&hashState, 0);
            XXH64_update(&hashState, fragBlobOut->GetBufferPointer(), fragBlobOut->GetBufferSize());
            uint64_t binKey = XXH64_digest(&hashState);
            factory.m_sourceToBinary[srcKey] = binKey;
            return binKey;
        }

        boo::ObjToken<IShaderPipeline> newShaderPipeline
            (const char* vertSource, const char* fragSource,
             ComPtr<ID3DBlob>* vertBlobOut, ComPtr<ID3DBlob>* fragBlobOut,
             ComPtr<ID3DBlob>* pipelineBlob, const boo::ObjToken<IVertexFormat>& vtxFmt,
             BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
             ZTest depthTest, bool depthWrite, bool colorWrite,
             bool alphaWrite, CullMode culling, bool overwriteAlpha)
        {
            XXH64_state_t hashState;
            uint64_t srcHashes[2] = {};
            uint64_t binHashes[2] = {};
            XXH64_reset(&hashState, 0);
            if (vertSource)
            {
                XXH64_update(&hashState, vertSource, strlen(vertSource));
                srcHashes[0] = XXH64_digest(&hashState);
                auto binSearch = m_parent.m_sourceToBinary.find(srcHashes[0]);
                if (binSearch != m_parent.m_sourceToBinary.cend())
                    binHashes[0] = binSearch->second;
            }
            else if (vertBlobOut && *vertBlobOut)
            {
                XXH64_update(&hashState, (*vertBlobOut)->GetBufferPointer(), (*vertBlobOut)->GetBufferSize());
                binHashes[0] = XXH64_digest(&hashState);
            }
            XXH64_reset(&hashState, 0);
            if (fragSource)
            {
                XXH64_update(&hashState, fragSource, strlen(fragSource));
                srcHashes[1] = XXH64_digest(&hashState);
                auto binSearch = m_parent.m_sourceToBinary.find(srcHashes[1]);
                if (binSearch != m_parent.m_sourceToBinary.cend())
                    binHashes[1] = binSearch->second;
            }
            else if (fragBlobOut && *fragBlobOut)
            {
                XXH64_update(&hashState, (*fragBlobOut)->GetBufferPointer(), (*fragBlobOut)->GetBufferSize());
                binHashes[1] = XXH64_digest(&hashState);
            }

            if (vertBlobOut && !*vertBlobOut)
                binHashes[0] = CompileVert(*vertBlobOut, vertSource, srcHashes[0], m_parent);

            if (fragBlobOut && !*fragBlobOut)
                binHashes[1] = CompileFrag(*fragBlobOut, fragSource, srcHashes[1], m_parent);


            struct D3D11Context* ctx = m_parent.m_ctx;
            D3D11ShareableShader::Token vertShader;
            D3D11ShareableShader::Token fragShader;
            auto vertFind = binHashes[0] ? m_parent.m_sharedShaders.find(binHashes[0]) :
                                           m_parent.m_sharedShaders.end();
            if (vertFind != m_parent.m_sharedShaders.end())
            {
                vertShader = vertFind->second->lock();
            }
            else
            {
                ComPtr<ID3DBlob> vertBlob;
                if (vertBlobOut)
                    vertBlob = *vertBlobOut;
                else
                    binHashes[0] = CompileVert(vertBlob, vertSource, srcHashes[0], m_parent);

                ComPtr<ID3D11VertexShader> vShader;
                ThrowIfFailed(ctx->m_dev->CreateVertexShader(vertBlob->GetBufferPointer(),
                                                             vertBlob->GetBufferSize(), nullptr, &vShader));

                auto it =
                m_parent.m_sharedShaders.emplace(std::make_pair(binHashes[0],
                    std::make_unique<D3D11ShareableShader>(m_parent, srcHashes[0], binHashes[0],
                                                           std::move(vShader), std::move(vertBlob)))).first;
                vertShader = it->second->lock();
            }
            auto fragFind = binHashes[1] ? m_parent.m_sharedShaders.find(binHashes[1]) :
                                           m_parent.m_sharedShaders.end();
            if (fragFind != m_parent.m_sharedShaders.end())
            {
                fragShader = fragFind->second->lock();
            }
            else
            {
                ComPtr<ID3DBlob> fragBlob;
                ComPtr<ID3DBlob>* useFragBlob;
                if (fragBlobOut)
                {
                    useFragBlob = fragBlobOut;
                }
                else
                {
                    useFragBlob = &fragBlob;
                    binHashes[1] = CompileFrag(fragBlob, fragSource, srcHashes[1], m_parent);
                }

                ComPtr<ID3D11PixelShader> pShader;
                ThrowIfFailed(ctx->m_dev->CreatePixelShader((*useFragBlob)->GetBufferPointer(),
                                                            (*useFragBlob)->GetBufferSize(), nullptr, &pShader));

                auto it =
                m_parent.m_sharedShaders.emplace(std::make_pair(binHashes[1],
                    std::make_unique<D3D11ShareableShader>(m_parent, srcHashes[1], binHashes[1],
                                                           std::move(pShader)))).first;
                fragShader = it->second->lock();
            }

            return {new D3D11ShaderPipeline(m_data, ctx,
                std::move(vertShader), std::move(fragShader),
                vtxFmt, srcFac, dstFac, prim, depthTest, depthWrite, colorWrite,
                alphaWrite, overwriteAlpha, culling)};
        }

        boo::ObjToken<IShaderDataBinding> newShaderDataBinding(
                const boo::ObjToken<IShaderPipeline>& pipeline,
                const boo::ObjToken<IVertexFormat>& vtxFormat,
                const boo::ObjToken<IGraphicsBuffer>& vbuf,
                const boo::ObjToken<IGraphicsBuffer>& instVbo,
                const boo::ObjToken<IGraphicsBuffer>& ibuf,
                size_t ubufCount, const boo::ObjToken<IGraphicsBuffer>* ubufs, const PipelineStage* ubufStages,
                const size_t* ubufOffs, const size_t* ubufSizes,
                size_t texCount, const boo::ObjToken<ITexture>* texs,
                const int* texBindIdx, const bool* depthBind,
                size_t baseVert, size_t baseInst)
        {
            return {new D3D11ShaderDataBinding(m_data, m_parent.m_ctx, pipeline, vbuf, instVbo, ibuf,
                                               ubufCount, ubufs, ubufStages, ubufOffs, ubufSizes, texCount, texs,
                                               texBindIdx, depthBind, baseVert, baseInst)};
        }
    };

    boo::ObjToken<IGraphicsBufferD> newPoolBuffer(BufferUse use, size_t stride, size_t count)
    {
        D3D11CommandQueue* q = static_cast<D3D11CommandQueue*>(m_parent->getCommandQueue());
        boo::ObjToken<BaseGraphicsPool> pool(new BaseGraphicsPool(*this));
        return {new D3D11GraphicsBufferD<BaseGraphicsPool>(pool, q, use, m_ctx, stride, count)};
    }

    void commitTransaction(const FactoryCommitFunc& trans)
    {
        D3D11DataFactory::Context ctx(*this);
        trans(ctx);
    }

    void _unregisterShareableShader(uint64_t srcKey, uint64_t binKey)
    {
        if (srcKey)
            m_sourceToBinary.erase(srcKey);
        m_sharedShaders.erase(binKey);
    }

    void setDisplayGamma(float gamma)
    {
        if (m_ctx->m_fbFormat == DXGI_FORMAT_R16G16B16A16_FLOAT)
            m_gamma = gamma * 2.2f;
        else
            m_gamma = gamma;
        if (m_gamma != 1.f)
            UpdateGammaLUT(m_gammaLUT.get(), m_gamma);
    }
};

void D3D11CommandQueue::RenderingWorker(D3D11CommandQueue* self)
{
    {
        std::unique_lock<std::mutex> lk(self->m_initmt);
    }
    self->m_initcv.notify_one();
    D3D11DataFactory* dataFactory = static_cast<D3D11DataFactory*>(self->m_parent->getDataFactory());
    while (self->m_running)
    {
        {
            std::unique_lock<std::mutex> lk(self->m_mt);
            self->m_cv.wait(lk);
            if (!self->m_running)
                break;
            self->m_drawBuf = self->m_completeBuf;
            auto& CmdList = self->m_cmdLists[self->m_drawBuf];

            self->ProcessDynamicLoads(self->m_ctx->m_devCtx.Get());

            if (self->m_texResizes.size())
            {
                for (const auto& resize : self->m_texResizes)
                    resize.first->resize(self->m_ctx, resize.second.first, resize.second.second);
                self->m_texResizes.clear();
                CmdList.reset();
                continue;
            }

            if (self->m_windowCtx->m_needsFSTransition)
            {
                if (self->m_windowCtx->m_fs)
                {
                    self->m_windowCtx->m_swapChain->SetFullscreenState(true, nullptr);
                    self->m_windowCtx->m_swapChain->ResizeTarget(&self->m_windowCtx->m_fsdesc);
                }
                else
                    self->m_windowCtx->m_swapChain->SetFullscreenState(false, nullptr);

                self->m_windowCtx->m_needsFSTransition = false;
                CmdList.reset();
                continue;
            }

            if (self->m_windowCtx->m_needsResize)
            {
                self->m_windowCtx->clearRTV();
                self->m_windowCtx->m_swapChain->ResizeBuffers(2,
                    self->m_windowCtx->width, self->m_windowCtx->height,
                    self->m_ctx->m_fbFormat, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
                self->m_windowCtx->setupRTV(self->m_windowCtx->m_swapChain, self->m_ctx->m_dev.Get());
                self->m_windowCtx->m_needsResize = false;
                CmdList.reset();
                continue;
            }
        }

        auto& CmdList = self->m_cmdLists[self->m_drawBuf];
        ID3D11CommandList* list = CmdList.list.Get();
        self->m_ctx->m_devCtx->ExecuteCommandList(list, false);

        if (D3D11TextureR* csource = CmdList.workDoPresent.cast<D3D11TextureR>())
        {
#ifndef NDEBUG
            if (!csource->m_colorBindCount)
                Log.report(logvisor::Fatal,
                           "texture provided to resolveDisplay() must have at least 1 color binding");
#endif

            if (dataFactory->m_gamma != 1.f)
            {
                SWindowRect rect(0, 0, csource->m_width, csource->m_height);
                self->_resolveBindTexture(self->m_ctx->m_devCtx.Get(), csource, rect, true, 0, true, false);
                ID3D11RenderTargetView* rtv = self->m_windowCtx->m_swapChainRTV.Get();
                self->m_ctx->m_devCtx->OMSetRenderTargets(1, &rtv, nullptr);

                D3D11_VIEWPORT vp = {0.f, 0.f, FLOAT(csource->m_width), FLOAT(csource->m_height), 0.f, 1.f};
                self->m_ctx->m_devCtx->RSSetViewports(1, &vp);
                D3D11_RECT d3drect = {0, 0, LONG(csource->m_width), LONG(csource->m_height)};
                self->m_ctx->m_devCtx->RSSetScissorRects(1, &d3drect);
                ID3D11SamplerState* samp[] = {self->m_ctx->m_ss[0].Get(),
                                              self->m_ctx->m_ss[1].Get(),
                                              self->m_ctx->m_ss[2].Get(),
                                              self->m_ctx->m_ss[3].Get()};
                self->m_ctx->m_devCtx->PSSetSamplers(0, 4, samp);

                D3D11ShaderDataBinding* gammaBinding = dataFactory->m_gammaBinding.cast<D3D11ShaderDataBinding>();
                gammaBinding->m_texs[0].tex = CmdList.workDoPresent.get();
                gammaBinding->bind(self->m_ctx->m_devCtx.Get(), self->m_drawBuf);
                self->m_ctx->m_devCtx->Draw(4, 0);
                gammaBinding->m_texs[0].tex.reset();
            }
            else
            {
                ComPtr<ID3D11Texture2D> dest = self->m_windowCtx->m_swapChainTex;
                ID3D11Texture2D* src = csource->m_colorTex.Get();
                if (csource->m_samples > 1)
                    self->m_ctx->m_devCtx->ResolveSubresource(dest.Get(), 0, src, 0, self->m_ctx->m_fbFormat);
                else
                    self->m_ctx->m_devCtx->CopyResource(dest.Get(), src);
            }

            self->m_windowCtx->m_swapChain->Present(1, 0);
        }

        CmdList.reset();
    }
}

void D3D11CommandQueue::startRenderer()
{
    static_cast<D3D11DataFactory*>(m_parent->getDataFactory())->SetupGammaResources();
}

void D3D11CommandQueue::execute()
{
    /* Finish command list */
    auto& CmdList = m_cmdLists[m_fillBuf];
    ThrowIfFailed(m_deferredCtx->FinishCommandList(false, &CmdList.list));
    CmdList.workDoPresent = m_doPresent;
    m_doPresent = nullptr;

    /* Wait for worker thread to become ready */
    std::unique_lock<std::mutex> lk(m_mt);

    /* Ready for next frame */
    m_completeBuf = m_fillBuf;
    for (size_t i=0 ; i<3 ; ++i)
    {
        if (i == m_completeBuf || i == m_drawBuf)
            continue;
        m_fillBuf = i;
        break;
    }

    /* Return control to worker thread */
    lk.unlock();
    m_cv.notify_one();
}

void D3D11CommandQueue::ProcessDynamicLoads(ID3D11DeviceContext* ctx)
{
    D3D11DataFactory* gfxF = static_cast<D3D11DataFactory*>(m_parent->getDataFactory());
    std::unique_lock<std::recursive_mutex> lk(m_dynamicLock);
    std::unique_lock<std::recursive_mutex> datalk(gfxF->m_dataMutex);

    if (gfxF->m_dataHead)
    {
        for (BaseGraphicsData& d : *gfxF->m_dataHead)
        {
            if (d.m_DBufs)
                for (IGraphicsBufferD& b : *d.m_DBufs)
                    static_cast<D3D11GraphicsBufferD<BaseGraphicsData>&>(b).update(ctx, m_drawBuf);
            if (d.m_DTexs)
                for (ITextureD& t : *d.m_DTexs)
                    static_cast<D3D11TextureD&>(t).update(ctx, m_drawBuf);
        }
    }
    if (gfxF->m_poolHead)
    {
        for (BaseGraphicsPool& p : *gfxF->m_poolHead)
        {
            if (p.m_DBufs)
                for (IGraphicsBufferD& b : *p.m_DBufs)
                    static_cast<D3D11GraphicsBufferD<BaseGraphicsData>&>(b).update(ctx, m_drawBuf);
        }
    }
}

IGraphicsCommandQueue* _NewD3D11CommandQueue(D3D11Context* ctx, D3D11Context::Window* windowCtx, IGraphicsContext* parent)
{
    return new D3D11CommandQueue(ctx, windowCtx, parent);
}

IGraphicsDataFactory* _NewD3D11DataFactory(D3D11Context* ctx, IGraphicsContext* parent)
{
    return new D3D11DataFactory(parent, ctx);
}

}
