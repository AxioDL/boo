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
        _com_error err(hr);
        LPCTSTR errMsg = err.ErrorMessage();
        Log.report(logvisor::Fatal, errMsg);
    }
}

struct D3D11Data : IGraphicsDataPriv<D3D11Data>
{
    std::vector<std::unique_ptr<class D3D11ShaderPipeline>> m_SPs;
    std::vector<std::unique_ptr<struct D3D11ShaderDataBinding>> m_SBinds;
    std::vector<std::unique_ptr<class D3D11GraphicsBufferS>> m_SBufs;
    std::vector<std::unique_ptr<class D3D11GraphicsBufferD>> m_DBufs;
    std::vector<std::unique_ptr<class D3D11TextureS>> m_STexs;
    std::vector<std::unique_ptr<class D3D11TextureSA>> m_SATexs;
    std::vector<std::unique_ptr<class D3D11TextureD>> m_DTexs;
    std::vector<std::unique_ptr<class D3D11TextureR>> m_RTexs;
    std::vector<std::unique_ptr<struct D3D11VertexFormat>> m_VFmts;
};

class D3D11GraphicsBufferD;
struct D3D11Pool : IGraphicsBufferPool
{
    std::unordered_map<D3D11GraphicsBufferD*, std::unique_ptr<D3D11GraphicsBufferD>> m_DBufs;
};

static const D3D11_BIND_FLAG USE_TABLE[] =
{
    D3D11_BIND_VERTEX_BUFFER,
    D3D11_BIND_VERTEX_BUFFER,
    D3D11_BIND_INDEX_BUFFER,
    D3D11_BIND_CONSTANT_BUFFER
};

class D3D11GraphicsBufferS : public IGraphicsBufferS
{
    friend class D3D11DataFactory;
    friend struct D3D11CommandQueue;

    size_t m_sz;
    D3D11GraphicsBufferS(BufferUse use, D3D11Context* ctx, const void* data, size_t stride, size_t count)
    : m_stride(stride), m_count(count), m_sz(stride * count)
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

class D3D11GraphicsBufferD : public IGraphicsBufferD
{
    friend class D3D11DataFactory;
    friend struct D3D11CommandQueue;

    D3D11CommandQueue* m_q;
    std::unique_ptr<uint8_t[]> m_cpuBuf;
    size_t m_cpuSz;
    int m_validSlots = 0;
    D3D11GraphicsBufferD(D3D11CommandQueue* q, BufferUse use, D3D11Context* ctx, size_t stride, size_t count)
        : m_q(q), m_stride(stride), m_count(count)
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

class D3D11TextureS : public ITextureS
{
    friend class D3D11DataFactory;
    size_t m_sz;

    D3D11TextureS(D3D11Context* ctx, size_t width, size_t height, size_t mips,
                  TextureFormat fmt, const void* data, size_t sz)
    : m_sz(sz)
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

class D3D11TextureSA : public ITextureSA
{
    friend class D3D11DataFactory;

    size_t m_sz;
    D3D11TextureSA(D3D11Context* ctx, size_t width, size_t height, size_t layers,
                   size_t mips, TextureFormat fmt, const void* data, size_t sz)
    : m_sz(sz)
    {
        size_t pixelPitch;
        DXGI_FORMAT pixelFmt;
        if (fmt == TextureFormat::RGBA8)
        {
            pixelPitch = 4;
            pixelFmt = DXGI_FORMAT_R8G8B8A8_UNORM;
        }
        else if (fmt == TextureFormat::I8)
        {
            pixelPitch = 1;
            pixelFmt = DXGI_FORMAT_R8_UNORM;
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

class D3D11TextureD : public ITextureD
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
    D3D11TextureD(D3D11CommandQueue* q, D3D11Context* ctx, size_t width, size_t height, TextureFormat fmt)
    : m_width(width), m_height(height), m_q(q)
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

class D3D11TextureR : public ITextureR
{
    friend class D3D11DataFactory;
    friend struct D3D11CommandQueue;

    size_t m_width = 0;
    size_t m_height = 0;
    size_t m_samples = 0;
    bool m_enableShaderColorBind;
    bool m_enableShaderDepthBind;

    void Setup(D3D11Context* ctx, size_t width, size_t height, size_t samples,
               bool enableShaderColorBind, bool enableShaderDepthBind)
    {
        ThrowIfFailed(ctx->m_dev->CreateTexture2D(&CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R8G8B8A8_UNORM, width, height,
            1, 1, D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0, samples), nullptr, &m_colorTex));
        ThrowIfFailed(ctx->m_dev->CreateTexture2D(&CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_D24_UNORM_S8_UINT, width, height,
            1, 1, D3D11_BIND_DEPTH_STENCIL, D3D11_USAGE_DEFAULT, 0, samples), nullptr, &m_depthTex));

        D3D11_RTV_DIMENSION rtvDim;
        D3D11_DSV_DIMENSION dsvDim;
        D3D11_SRV_DIMENSION srvDim;

        if (samples > 1)
        {
            rtvDim = D3D11_RTV_DIMENSION_TEXTURE2DMS;
            dsvDim = D3D11_DSV_DIMENSION_TEXTURE2DMS;
            srvDim = D3D11_SRV_DIMENSION_TEXTURE2DMS;
        }
        else
        {
            rtvDim = D3D11_RTV_DIMENSION_TEXTURE2D;
            dsvDim = D3D11_DSV_DIMENSION_TEXTURE2D;
            srvDim = D3D11_SRV_DIMENSION_TEXTURE2D;
        }

        ThrowIfFailed(ctx->m_dev->CreateRenderTargetView(m_colorTex.Get(),
            &CD3D11_RENDER_TARGET_VIEW_DESC(m_colorTex.Get(), rtvDim), &m_rtv));
        ThrowIfFailed(ctx->m_dev->CreateDepthStencilView(m_depthTex.Get(),
            &CD3D11_DEPTH_STENCIL_VIEW_DESC(m_depthTex.Get(), dsvDim), &m_dsv));

        if (enableShaderColorBind)
        {
            ThrowIfFailed(ctx->m_dev->CreateTexture2D(&CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R8G8B8A8_UNORM, width, height,
                1, 1, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, samples), nullptr, &m_colorBindTex));
            ThrowIfFailed(ctx->m_dev->CreateShaderResourceView(m_colorBindTex.Get(),
                &CD3D11_SHADER_RESOURCE_VIEW_DESC(m_colorBindTex.Get(), srvDim), &m_colorSrv));
        }

        if (enableShaderDepthBind)
        {
            ThrowIfFailed(ctx->m_dev->CreateTexture2D(&CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R24G8_TYPELESS, width, height,
                1, 1, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, samples), nullptr, &m_depthBindTex));
            ThrowIfFailed(ctx->m_dev->CreateShaderResourceView(m_depthBindTex.Get(),
                &CD3D11_SHADER_RESOURCE_VIEW_DESC(m_depthBindTex.Get(), srvDim, DXGI_FORMAT_R24G8_TYPELESS), &m_depthSrv));
        }
    }

    D3D11TextureR(D3D11Context* ctx, size_t width, size_t height, size_t samples,
                  bool enableShaderColorBind, bool enableShaderDepthBind)
        : m_width(width), m_height(height), m_samples(samples),
          m_enableShaderColorBind(enableShaderColorBind), m_enableShaderDepthBind(enableShaderDepthBind)
    {
        if (samples == 0) m_samples = 1;
        Setup(ctx, width, height, samples, enableShaderColorBind, enableShaderDepthBind);
    }
public:
    size_t samples() const {return m_samples;}
    ComPtr<ID3D11Texture2D> m_colorTex;
    ComPtr<ID3D11RenderTargetView> m_rtv;

    ComPtr<ID3D11Texture2D> m_depthTex;
    ComPtr<ID3D11DepthStencilView> m_dsv;

    ComPtr<ID3D11Texture2D> m_colorBindTex;
    ComPtr<ID3D11ShaderResourceView> m_colorSrv;

    ComPtr<ID3D11Texture2D> m_depthBindTex;
    ComPtr<ID3D11ShaderResourceView> m_depthSrv;

    ~D3D11TextureR() = default;

    void resize(D3D11Context* ctx, size_t width, size_t height)
    {
        if (width < 1)
            width = 1;
        if (height < 1)
            height = 1;
        m_width = width;
        m_height = height;
        Setup(ctx, width, height, m_samples, m_enableShaderColorBind, m_enableShaderDepthBind);
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

struct D3D11VertexFormat : IVertexFormat
{
    size_t m_elementCount;
    std::unique_ptr<D3D11_INPUT_ELEMENT_DESC[]> m_elements;
    size_t m_stride = 0;
    size_t m_instStride = 0;

    D3D11VertexFormat(size_t elementCount, const VertexElementDescriptor* elements)
        : m_elementCount(elementCount),
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

class D3D11ShaderPipeline : public IShaderPipeline
{
    friend class D3D11DataFactory;
    friend struct D3D11ShaderDataBinding;
    const D3D11VertexFormat* m_vtxFmt;
    D3D11ShareableShader::Token m_vert;
    D3D11ShareableShader::Token m_pixel;

    D3D11ShaderPipeline(D3D11Context* ctx,
        D3D11ShareableShader::Token&& vert,
        D3D11ShareableShader::Token&& pixel,
        const D3D11VertexFormat* vtxFmt,
        BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
        bool depthTest, bool depthWrite, CullMode culling)
    : m_vtxFmt(vtxFmt), m_vert(std::move(vert)), m_pixel(std::move(pixel)),
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
            D3D11_DEFAULT_DEPTH_BIAS, D3D11_DEFAULT_DEPTH_BIAS_CLAMP, D3D11_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
            true, true, false, false);
        ThrowIfFailed(ctx->m_dev->CreateRasterizerState(&rasDesc, &m_rasState));

        CD3D11_DEPTH_STENCIL_DESC dsDesc(D3D11_DEFAULT);
        dsDesc.DepthEnable = depthTest;
        dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK(depthWrite);
        dsDesc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
        ThrowIfFailed(ctx->m_dev->CreateDepthStencilState(&dsDesc, &m_dsState));

        CD3D11_BLEND_DESC blDesc(D3D11_DEFAULT);
        blDesc.RenderTarget[0].BlendEnable = (dstFac != BlendFactor::Zero);
        blDesc.RenderTarget[0].SrcBlend = BLEND_FACTOR_TABLE[int(srcFac)];
        blDesc.RenderTarget[0].DestBlend = BLEND_FACTOR_TABLE[int(dstFac)];
        ThrowIfFailed(ctx->m_dev->CreateBlendState(&blDesc, &m_blState));

        const auto& vertBuf = m_vert.get().m_vtxBlob;
        ThrowIfFailed(ctx->m_dev->CreateInputLayout(vtxFmt->m_elements.get(), vtxFmt->m_elementCount,
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

struct D3D11ShaderDataBinding : IShaderDataBindingPriv<D3D11Data>
{
    D3D11ShaderPipeline* m_pipeline;
    IGraphicsBuffer* m_vbuf;
    IGraphicsBuffer* m_instVbuf;
    IGraphicsBuffer* m_ibuf;
    std::vector<IGraphicsBuffer*> m_ubufs;
    std::unique_ptr<UINT[]> m_ubufFirstConsts;
    std::unique_ptr<UINT[]> m_ubufNumConsts;
    std::unique_ptr<bool[]> m_pubufs;
    std::vector<ITexture*> m_texs;
    UINT m_baseOffsets[2];

    D3D11ShaderDataBinding(D3D11Data* d,
                           D3D11Context* ctx,
                           IShaderPipeline* pipeline,
                           IGraphicsBuffer* vbuf, IGraphicsBuffer* instVbuf, IGraphicsBuffer* ibuf,
                           size_t ubufCount, IGraphicsBuffer** ubufs, const PipelineStage* ubufStages,
                           const size_t* ubufOffs, const size_t* ubufSizes,
                           size_t texCount, ITexture** texs, size_t baseVert, size_t baseInst)
    : IShaderDataBindingPriv(d),
      m_pipeline(static_cast<D3D11ShaderPipeline*>(pipeline)),
      m_vbuf(vbuf),
      m_instVbuf(instVbuf),
      m_ibuf(ibuf)
    {
        m_ubufs.reserve(ubufCount);
        m_texs.reserve(texCount);

        m_baseOffsets[0] = UINT(baseVert * m_pipeline->m_vtxFmt->m_stride);
        m_baseOffsets[1] = UINT(baseInst * m_pipeline->m_vtxFmt->m_instStride);

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
            m_texs.push_back(texs[i]);
        }
    }

    void bind(ID3D11DeviceContext1* ctx, int b)
    {
        m_pipeline->bind(ctx);

        ID3D11Buffer* bufs[2] = {};
        UINT strides[2] = {};

        if (m_vbuf)
        {
            if (m_vbuf->dynamic())
            {
                D3D11GraphicsBufferD* cbuf = static_cast<D3D11GraphicsBufferD*>(m_vbuf);
                bufs[0] = cbuf->m_bufs[b].Get();
                strides[0] = UINT(cbuf->m_stride);
            }
            else
            {
                D3D11GraphicsBufferS* cbuf = static_cast<D3D11GraphicsBufferS*>(m_vbuf);
                bufs[0] = cbuf->m_buf.Get();
                strides[0] = UINT(cbuf->m_stride);
            }
        }

        if (m_instVbuf)
        {
            if (m_instVbuf->dynamic())
            {
                D3D11GraphicsBufferD* cbuf = static_cast<D3D11GraphicsBufferD*>(m_instVbuf);
                bufs[1] = cbuf->m_bufs[b].Get();
                strides[1] = UINT(cbuf->m_stride);
            }
            else
            {
                D3D11GraphicsBufferS* cbuf = static_cast<D3D11GraphicsBufferS*>(m_instVbuf);
                bufs[1] = cbuf->m_buf.Get();
                strides[1] = UINT(cbuf->m_stride);
            }
        }

        ctx->IASetVertexBuffers(0, 2, bufs, strides, m_baseOffsets);

        if (m_ibuf)
        {
            if (m_ibuf->dynamic())
            {
                D3D11GraphicsBufferD* cbuf = static_cast<D3D11GraphicsBufferD*>(m_ibuf);
                ctx->IASetIndexBuffer(cbuf->m_bufs[b].Get(), DXGI_FORMAT_R32_UINT, 0);
            }
            else
            {
                D3D11GraphicsBufferS* cbuf = static_cast<D3D11GraphicsBufferS*>(m_ibuf);
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
                        D3D11GraphicsBufferD* cbuf = static_cast<D3D11GraphicsBufferD*>(m_ubufs[i]);
                        constBufs[i] = cbuf->m_bufs[b].Get();
                    }
                    else
                    {
                        D3D11GraphicsBufferS* cbuf = static_cast<D3D11GraphicsBufferS*>(m_ubufs[i]);
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
                            D3D11GraphicsBufferD* cbuf = static_cast<D3D11GraphicsBufferD*>(m_ubufs[i]);
                            constBufs[i] = cbuf->m_bufs[b].Get();
                        }
                        else
                        {
                            D3D11GraphicsBufferS* cbuf = static_cast<D3D11GraphicsBufferS*>(m_ubufs[i]);
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
                        D3D11GraphicsBufferD* cbuf = static_cast<D3D11GraphicsBufferD*>(m_ubufs[i]);
                        constBufs[i] = cbuf->m_bufs[b].Get();
                    }
                    else
                    {
                        D3D11GraphicsBufferS* cbuf = static_cast<D3D11GraphicsBufferS*>(m_ubufs[i]);
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
                            D3D11GraphicsBufferD* cbuf = static_cast<D3D11GraphicsBufferD*>(m_ubufs[i]);
                            constBufs[i] = cbuf->m_bufs[b].Get();
                        }
                        else
                        {
                            D3D11GraphicsBufferS* cbuf = static_cast<D3D11GraphicsBufferS*>(m_ubufs[i]);
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
                if (m_texs[i])
                {
                    switch (m_texs[i]->type())
                    {
                    case TextureType::Dynamic:
                    {
                        D3D11TextureD* ctex = static_cast<D3D11TextureD*>(m_texs[i]);
                        srvs[i] = ctex->m_srvs[b].Get();
                        break;
                    }
                    case TextureType::Static:
                    {
                        D3D11TextureS* ctex = static_cast<D3D11TextureS*>(m_texs[i]);
                        srvs[i] = ctex->m_srv.Get();
                        break;
                    }
                    case TextureType::StaticArray:
                    {
                        D3D11TextureSA* ctex = static_cast<D3D11TextureSA*>(m_texs[i]);
                        srvs[i] = ctex->m_srv.Get();
                        break;
                    }
                    case TextureType::Render:
                    {
                        D3D11TextureR* ctex = static_cast<D3D11TextureR*>(m_texs[i]);
                        srvs[i] = ctex->m_colorSrv.Get();
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
        std::vector<IShaderDataBindingPriv<D3D11Data>::Token> resTokens;
        D3D11TextureR* workDoPresent = nullptr;

        void reset()
        {
            list.Reset();
            resTokens.clear();
            workDoPresent = nullptr;
        }
    };
    CommandList m_cmdLists[3];

    std::recursive_mutex m_dynamicLock;
    void ProcessDynamicLoads(ID3D11DeviceContext* ctx);
    static void RenderingWorker(D3D11CommandQueue* self)
    {
        {
            std::unique_lock<std::mutex> lk(self->m_initmt);
        }
        self->m_initcv.notify_one();
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
                    self->m_windowCtx->m_swapChain->ResizeBuffers(2,
                        self->m_windowCtx->width, self->m_windowCtx->height,
                        DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
                    self->m_windowCtx->m_needsResize = false;
                    CmdList.reset();
                    continue;
                }
            }

            auto& CmdList = self->m_cmdLists[self->m_drawBuf];
            ID3D11CommandList* list = CmdList.list.Get();
            self->m_ctx->m_devCtx->ExecuteCommandList(list, false);

            D3D11TextureR* csource = CmdList.workDoPresent;
            if (csource)
            {
                ComPtr<ID3D11Texture2D> dest;
                ThrowIfFailed(self->m_windowCtx->m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &dest));

                ID3D11Texture2D* src = csource->m_colorTex.Get();
                if (csource->m_samples > 1)
                    self->m_ctx->m_devCtx->ResolveSubresource(dest.Get(), 0, src, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
                else
                    self->m_ctx->m_devCtx->CopyResource(dest.Get(), src);

                self->m_windowCtx->m_swapChain->Present(1, 0);
            }

            CmdList.reset();
        }
    }

    D3D11CommandQueue(D3D11Context* ctx, D3D11Context::Window* windowCtx, IGraphicsContext* parent)
    : m_ctx(ctx), m_windowCtx(windowCtx), m_parent(parent),
      m_initlk(m_initmt),
      m_thr(RenderingWorker, this)
    {
        m_initcv.wait(m_initlk);
        m_initlk.unlock();
        ThrowIfFailed(ctx->m_dev->CreateDeferredContext1(0, &m_deferredCtx));
    }

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

    void setShaderDataBinding(IShaderDataBinding* binding)
    {
        D3D11ShaderDataBinding* cbind = static_cast<D3D11ShaderDataBinding*>(binding);
        cbind->bind(m_deferredCtx.Get(), m_fillBuf);
        m_cmdLists[m_fillBuf].resTokens.push_back(cbind->lock());

        ID3D11SamplerState* samp[] = {m_ctx->m_ss.Get()};
        m_deferredCtx->PSSetSamplers(0, 1, samp);
    }

    D3D11TextureR* m_boundTarget;
    void setRenderTarget(ITextureR* target)
    {
        D3D11TextureR* ctarget = static_cast<D3D11TextureR*>(target);
        ID3D11RenderTargetView* view[] = {ctarget->m_rtv.Get()};
        m_deferredCtx->OMSetRenderTargets(1, view, ctarget->m_dsv.Get());
        m_boundTarget = ctarget;
    }

    void setViewport(const SWindowRect& rect, float znear, float zfar)
    {
        if (m_boundTarget)
        {
            int boundHeight = m_boundTarget->m_height;
            D3D11_VIEWPORT vp = {FLOAT(rect.location[0]), FLOAT(boundHeight - rect.location[1] - rect.size[1]),
                                 FLOAT(rect.size[0]), FLOAT(rect.size[1]), znear, zfar};
            m_deferredCtx->RSSetViewports(1, &vp);
        }
    }

    void setScissor(const SWindowRect& rect)
    {
        if (m_boundTarget)
        {
            int boundHeight = m_boundTarget->m_height;
            D3D11_RECT d3drect = {LONG(rect.location[0]), LONG(boundHeight - rect.location[1] - rect.size[1]),
                                  LONG(rect.location[0] + rect.size[0]), LONG(boundHeight - rect.location[1])};
            m_deferredCtx->RSSetScissorRects(1, &d3drect);
        }
    }

    std::unordered_map<D3D11TextureR*, std::pair<size_t, size_t>> m_texResizes;
    void resizeRenderTexture(ITextureR* tex, size_t width, size_t height)
    {
        D3D11TextureR* ctex = static_cast<D3D11TextureR*>(tex);
        std::unique_lock<std::mutex> lk(m_mt);
        m_texResizes[ctex] = std::make_pair(width, height);
    }

    void schedulePostFrameHandler(std::function<void(void)>&& func)
    {
        func();
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
        if (render)
            m_deferredCtx->ClearRenderTargetView(m_boundTarget->m_rtv.Get(), m_clearColor);
        if (depth)
            m_deferredCtx->ClearDepthStencilView(m_boundTarget->m_dsv.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
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

    void resolveBindTexture(ITextureR* texture, const SWindowRect& rect, bool tlOrigin, bool color, bool depth)
    {
        const D3D11TextureR* tex = static_cast<const D3D11TextureR*>(texture);
        if (color && tex->m_enableShaderColorBind)
        {
            if (tex->m_samples > 1)
            {
                m_deferredCtx->CopyResource(tex->m_colorBindTex.Get(), tex->m_colorTex.Get());
            }
            else
            {
                SWindowRect intersectRect = rect.intersect(SWindowRect(0, 0, tex->m_width, tex->m_height));
                int y = tlOrigin ? intersectRect.location[1] : (tex->m_height - intersectRect.size[1] - intersectRect.location[1]);
                D3D11_BOX box = {UINT(intersectRect.location[0]), UINT(y), 0,
                                 UINT(intersectRect.location[0] + intersectRect.size[0]), UINT(y + intersectRect.size[1]), 1};
                m_deferredCtx->CopySubresourceRegion1(tex->m_colorBindTex.Get(), 0, box.left, box.top, 0,
                                                      tex->m_colorTex.Get(), 0, &box, D3D11_COPY_DISCARD);
            }
        }
        if (depth && tex->m_enableShaderDepthBind)
        {
            m_deferredCtx->CopyResource(tex->m_depthBindTex.Get(), tex->m_depthTex.Get());
        }
    }

    D3D11TextureR* m_doPresent;
    void resolveDisplay(ITextureR* source)
    {
        m_doPresent = static_cast<D3D11TextureR*>(source);
    }

    void execute();
};

void D3D11GraphicsBufferD::update(ID3D11DeviceContext* ctx, int b)
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
void D3D11GraphicsBufferD::load(const void* data, size_t sz)
{
    std::unique_lock<std::recursive_mutex> lk(m_q->m_dynamicLock);
    size_t bufSz = std::min(sz, m_cpuSz);
    memcpy(m_cpuBuf.get(), data, bufSz);
    m_validSlots = 0;
}
void* D3D11GraphicsBufferD::map(size_t sz)
{
    if (sz > m_cpuSz)
        return nullptr;
    m_q->m_dynamicLock.lock();
    return m_cpuBuf.get();
}
void D3D11GraphicsBufferD::unmap()
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

class D3D11DataFactory : public ID3DDataFactory
{
    friend struct D3D11CommandQueue;
    IGraphicsContext* m_parent;
    static thread_local D3D11Data* m_deferredData;
    struct D3D11Context* m_ctx;
    std::unordered_set<D3D11Data*> m_committedData;
    std::unordered_set<D3D11Pool*> m_committedPools;
    std::mutex m_committedMutex;
    std::unordered_map<uint64_t, std::unique_ptr<D3D11ShareableShader>> m_sharedShaders;
    std::unordered_map<uint64_t, uint64_t> m_sourceToBinary;
    uint32_t m_sampleCount;

    void destroyData(IGraphicsData* d)
    {
        std::unique_lock<std::mutex> lk(m_committedMutex);
        D3D11Data* data = static_cast<D3D11Data*>(d);
        m_committedData.erase(data);
        data->decrement();
    }

    void destroyAllData()
    {
        std::unique_lock<std::mutex> lk(m_committedMutex);
        for (D3D11Data* data : m_committedData)
            data->decrement();
        for (IGraphicsBufferPool* pool : m_committedPools)
            delete static_cast<D3D11Pool*>(pool);
        m_committedData.clear();
        m_committedPools.clear();
    }

    void destroyPool(IGraphicsBufferPool* p)
    {
        std::unique_lock<std::mutex> lk(m_committedMutex);
        D3D11Pool* pool = static_cast<D3D11Pool*>(p);
        m_committedPools.erase(pool);
        delete pool;
    }

    IGraphicsBufferD* newPoolBuffer(IGraphicsBufferPool* p, BufferUse use,
                                    size_t stride, size_t count)
    {
        D3D11CommandQueue* q = static_cast<D3D11CommandQueue*>(m_parent->getCommandQueue());
        D3D11Pool* pool = static_cast<D3D11Pool*>(p);
        D3D11GraphicsBufferD* retval = new D3D11GraphicsBufferD(q, use, m_ctx, stride, count);
        pool->m_DBufs.emplace(std::make_pair(retval, retval));
        return retval;
    }

    void deletePoolBuffer(IGraphicsBufferPool* p, IGraphicsBufferD* buf)
    {
        D3D11Pool* pool = static_cast<D3D11Pool*>(p);
        pool->m_DBufs.erase(static_cast<D3D11GraphicsBufferD*>(buf));
    }

public:
    D3D11DataFactory(IGraphicsContext* parent, D3D11Context* ctx, uint32_t sampleCount)
    : m_parent(parent), m_ctx(ctx), m_sampleCount(sampleCount)
    {}
    ~D3D11DataFactory() {destroyAllData();}

    Platform platform() const {return Platform::D3D11;}
    const SystemChar* platformName() const {return _S("D3D11");}

    class Context : public ID3DDataFactory::Context
    {
        friend class D3D11DataFactory;
        D3D11DataFactory& m_parent;
        Context(D3D11DataFactory& parent) : m_parent(parent) {}
    public:
        Platform platform() const {return Platform::D3D11;}
        const SystemChar* platformName() const {return _S("D3D11");}

        IGraphicsBufferS* newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count)
        {
            D3D11Data* d = static_cast<D3D11Data*>(m_deferredData);
            D3D11GraphicsBufferS* retval = new D3D11GraphicsBufferS(use, m_parent.m_ctx, data, stride, count);
            d->m_SBufs.emplace_back(retval);
            return retval;
        }

        IGraphicsBufferD* newDynamicBuffer(BufferUse use, size_t stride, size_t count)
        {
            D3D11Data* d = static_cast<D3D11Data*>(m_deferredData);
            D3D11CommandQueue* q = static_cast<D3D11CommandQueue*>(m_parent.m_parent->getCommandQueue());
            D3D11GraphicsBufferD* retval = new D3D11GraphicsBufferD(q, use, m_parent.m_ctx, stride, count);
            d->m_DBufs.emplace_back(retval);
            return retval;
        }

        ITextureS* newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
            const void* data, size_t sz)
        {
            D3D11Data* d = static_cast<D3D11Data*>(m_deferredData);
            D3D11TextureS* retval = new D3D11TextureS(m_parent.m_ctx, width, height, mips, fmt, data, sz);
            d->m_STexs.emplace_back(retval);
            return retval;
        }

        ITextureSA* newStaticArrayTexture(size_t width, size_t height, size_t layers, size_t mips,
                                          TextureFormat fmt, const void* data, size_t sz)
        {
            D3D11Data* d = static_cast<D3D11Data*>(m_deferredData);
            D3D11TextureSA* retval = new D3D11TextureSA(m_parent.m_ctx, width, height, layers, mips, fmt, data, sz);
            d->m_SATexs.emplace_back(retval);
            return retval;
        }

        ITextureD* newDynamicTexture(size_t width, size_t height, TextureFormat fmt)
        {
            D3D11Data* d = static_cast<D3D11Data*>(m_deferredData);
            D3D11CommandQueue* q = static_cast<D3D11CommandQueue*>(m_parent.m_parent->getCommandQueue());
            D3D11TextureD* retval = new D3D11TextureD(q, m_parent.m_ctx, width, height, fmt);
            d->m_DTexs.emplace_back(retval);
            return retval;
        }

        ITextureR* newRenderTexture(size_t width, size_t height,
                                    bool enableShaderColorBind, bool enableShaderDepthBind)
        {
            D3D11Data* d = static_cast<D3D11Data*>(m_deferredData);
            D3D11TextureR* retval = new D3D11TextureR(m_parent.m_ctx, width, height, m_parent.m_sampleCount,
                                                      enableShaderColorBind, enableShaderDepthBind);
            d->m_RTexs.emplace_back(retval);
            return retval;
        }

        IVertexFormat* newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements,
                                       size_t baseVert, size_t baseInst)
        {
            D3D11Data* d = static_cast<D3D11Data*>(m_deferredData);
            D3D11CommandQueue* q = static_cast<D3D11CommandQueue*>(m_parent.m_parent->getCommandQueue());
            D3D11VertexFormat* retval = new struct D3D11VertexFormat(elementCount, elements);
            d->m_VFmts.emplace_back(retval);
            return retval;
        }

#if _DEBUG
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
                Log.report(logvisor::Fatal, "error compiling pixel shader: %s", errBlob->GetBufferPointer());
            }

            XXH64_state_t hashState;
            XXH64_reset(&hashState, 0);
            XXH64_update(&hashState, fragBlobOut->GetBufferPointer(), fragBlobOut->GetBufferSize());
            uint64_t binKey = XXH64_digest(&hashState);
            factory.m_sourceToBinary[srcKey] = binKey;
            return binKey;
        }

        IShaderPipeline* newShaderPipeline
            (const char* vertSource, const char* fragSource,
             ComPtr<ID3DBlob>* vertBlobOut, ComPtr<ID3DBlob>* fragBlobOut,
             ComPtr<ID3DBlob>* pipelineBlob, IVertexFormat* vtxFmt,
             BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
             bool depthTest, bool depthWrite, CullMode culling)
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

            D3D11Data* d = static_cast<D3D11Data*>(m_deferredData);
            D3D11ShaderPipeline* retval = new D3D11ShaderPipeline(ctx,
                std::move(vertShader), std::move(fragShader),
                static_cast<const D3D11VertexFormat*>(vtxFmt),
                srcFac, dstFac, prim, depthTest, depthWrite, culling);
            d->m_SPs.emplace_back(retval);
            return retval;
        }

        IShaderDataBinding* newShaderDataBinding(IShaderPipeline* pipeline,
            IVertexFormat* vtxFormat,
            IGraphicsBuffer* vbuf, IGraphicsBuffer* instVbo, IGraphicsBuffer* ibuf,
            size_t ubufCount, IGraphicsBuffer** ubufs, const PipelineStage* ubufStages,
            const size_t* ubufOffs, const size_t* ubufSizes,
            size_t texCount, ITexture** texs,
            size_t baseVert, size_t baseInst)
        {
            D3D11Data* d = static_cast<D3D11Data*>(m_deferredData);
            D3D11ShaderDataBinding* retval =
                new D3D11ShaderDataBinding(d, m_parent.m_ctx, pipeline, vbuf, instVbo, ibuf,
                                           ubufCount, ubufs, ubufStages, ubufOffs, ubufSizes, texCount, texs,
                                           baseVert, baseInst);
            d->m_SBinds.emplace_back(retval);
            return retval;
        }
    };

    GraphicsDataToken commitTransaction(const FactoryCommitFunc& trans)
    {
        if (m_deferredData)
            Log.report(logvisor::Fatal, "nested commitTransaction usage detected");
        m_deferredData = new D3D11Data();

        D3D11DataFactory::Context ctx(*this);
        if (!trans(ctx))
        {
            delete m_deferredData;
            m_deferredData = nullptr;
            return GraphicsDataToken(this, nullptr);
        }

        std::unique_lock<std::mutex> lk(m_committedMutex);
        D3D11Data* retval = m_deferredData;
        m_deferredData = nullptr;
        m_committedData.insert(retval);
        lk.unlock();
        return GraphicsDataToken(this, retval);
    }

    GraphicsBufferPoolToken newBufferPool()
    {
        std::unique_lock<std::mutex> lk(m_committedMutex);
        D3D11Pool* retval = new D3D11Pool;
        m_committedPools.insert(retval);
        return GraphicsBufferPoolToken(this, retval);
    }

    void _unregisterShareableShader(uint64_t srcKey, uint64_t binKey)
    {
        if (srcKey)
            m_sourceToBinary.erase(srcKey);
        m_sharedShaders.erase(binKey);
    }
};

thread_local D3D11Data* D3D11DataFactory::m_deferredData;

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
    std::unique_lock<std::mutex> datalk(gfxF->m_committedMutex);

    for (D3D11Data* d : gfxF->m_committedData)
    {
        for (std::unique_ptr<D3D11GraphicsBufferD>& b : d->m_DBufs)
            b->update(ctx, m_drawBuf);
        for (std::unique_ptr<D3D11TextureD>& t : d->m_DTexs)
            t->update(ctx, m_drawBuf);
    }
    for (D3D11Pool* p : gfxF->m_committedPools)
    {
        for (auto& b : p->m_DBufs)
            b.second->update(ctx, m_drawBuf);
    }
}

IGraphicsCommandQueue* _NewD3D11CommandQueue(D3D11Context* ctx, D3D11Context::Window* windowCtx, IGraphicsContext* parent)
{
    return new D3D11CommandQueue(ctx, windowCtx, parent);
}

IGraphicsDataFactory* _NewD3D11DataFactory(D3D11Context* ctx, IGraphicsContext* parent, uint32_t sampleCount)
{
    return new D3D11DataFactory(parent, ctx, sampleCount);
}

}
