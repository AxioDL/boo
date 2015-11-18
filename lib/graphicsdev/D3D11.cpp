#include "../win/Win32Common.hpp"
#include <LogVisor/LogVisor.hpp>
#include "boo/graphicsdev/D3D.hpp"
#include "boo/IGraphicsContext.hpp"
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <d3dcompiler.h>
#include <comdef.h>

extern pD3DCompile D3DCompilePROC;

namespace boo
{
static LogVisor::LogModule Log("boo::D3D11");

static inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        // Set a breakpoint on this line to catch Win32 API errors.
        _com_error err(hr);
        LPCTSTR errMsg = err.ErrorMessage();
        Log.report(LogVisor::FatalError, errMsg);
    }
}

struct D3D11Data : IGraphicsData
{
    std::vector<std::unique_ptr<class D3D11ShaderPipeline>> m_SPs;
    std::vector<std::unique_ptr<struct D3D11ShaderDataBinding>> m_SBinds;
    std::vector<std::unique_ptr<class D3D11GraphicsBufferS>> m_SBufs;
    std::vector<std::unique_ptr<class D3D11GraphicsBufferD>> m_DBufs;
    std::vector<std::unique_ptr<class D3D11TextureS>> m_STexs;
    std::vector<std::unique_ptr<class D3D11TextureD>> m_DTexs;
    std::vector<std::unique_ptr<class D3D11TextureR>> m_RTexs;
    std::vector<std::unique_ptr<struct D3D11VertexFormat>> m_VFmts;
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
        ThrowIfFailed(ctx->m_dev->CreateBuffer(&CD3D11_BUFFER_DESC(m_sz, USE_TABLE[use], D3D11_USAGE_IMMUTABLE), &iData, &m_buf));
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
    D3D11GraphicsBufferD(D3D11CommandQueue* q, BufferUse use, D3D11Context* ctx, size_t stride, size_t count)
        : m_q(q), m_stride(stride), m_count(count)
    {
        size_t sz = stride * count;
        for (int i=0 ; i<3 ; ++i)
            ThrowIfFailed(ctx->m_dev->CreateBuffer(&CD3D11_BUFFER_DESC(sz, USE_TABLE[use],
                          D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE), nullptr, &m_bufs[i]));
    }
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
        CD3D11_TEXTURE2D_DESC desc(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, mips, 
            D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_IMMUTABLE);

        const uint8_t* dataIt = static_cast<const uint8_t*>(data);
        D3D11_SUBRESOURCE_DATA upData[16] = {};
        for (size_t i=0 ; i<mips && i<16 ; ++i)
        {
            upData[i].pSysMem = dataIt;
            upData[i].SysMemPitch = width * 4;
            upData[i].SysMemSlicePitch = upData[i].SysMemPitch * height;
            dataIt += upData[i].SysMemSlicePitch;
            width /= 2;
            height /= 2;
        }

        ThrowIfFailed(ctx->m_dev->CreateTexture2D(&desc, upData, &m_tex));
        ThrowIfFailed(ctx->m_dev->CreateShaderResourceView(m_tex.Get(), 
            &CD3D11_SHADER_RESOURCE_VIEW_DESC(m_tex.Get(), D3D_SRV_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM), &m_srv));
    }
public:
    ComPtr<ID3D11Texture2D> m_tex;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    ~D3D11TextureS() = default;
};

class D3D11TextureD : public ITextureD
{
    friend class D3D11DataFactory;
    friend struct D3D11CommandQueue;
    size_t m_width = 0;
    size_t m_height = 0;
    D3D11CommandQueue* m_q;
    D3D11TextureD(D3D11CommandQueue* q, D3D11Context* ctx, size_t width, size_t height, TextureFormat fmt)
        : m_q(q) 
    {
        CD3D11_TEXTURE2D_DESC desc(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1, 
            D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, D3D11_CPU_ACCESS_WRITE);
        for (int i=0 ; i<3 ; ++i)
        {
            ThrowIfFailed(ctx->m_dev->CreateTexture2D(&desc, nullptr, &m_texs[i]));
            ThrowIfFailed(ctx->m_dev->CreateShaderResourceView(m_texs[i].Get(), 
                &CD3D11_SHADER_RESOURCE_VIEW_DESC(m_texs[i].Get(), D3D_SRV_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM), &m_srvs[i]));
        }
    }
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

    void Setup(D3D11Context* ctx, size_t width, size_t height, size_t samples)
    {
        ThrowIfFailed(ctx->m_dev->CreateTexture2D(&CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 
            1, 1, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, 1), nullptr, &m_tex));
        ThrowIfFailed(ctx->m_dev->CreateTexture2D(&CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_D24_UNORM_S8_UINT, width, height, 
            1, 1, D3D11_BIND_DEPTH_STENCIL, D3D11_USAGE_DEFAULT, 0, samples), nullptr, &m_depthTex));

        if (samples > 1)
        {
            ThrowIfFailed(ctx->m_dev->CreateTexture2D(&CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 
                1, 1, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, samples), nullptr, &m_msaaTex));
            ThrowIfFailed(ctx->m_dev->CreateRenderTargetView(m_msaaTex.Get(), 
                &CD3D11_RENDER_TARGET_VIEW_DESC(m_msaaTex.Get(), D3D11_RTV_DIMENSION_TEXTURE2DMS), &m_rtv));
            ThrowIfFailed(ctx->m_dev->CreateDepthStencilView(m_depthTex.Get(), 
                &CD3D11_DEPTH_STENCIL_VIEW_DESC(m_depthTex.Get(), D3D11_DSV_DIMENSION_TEXTURE2DMS), &m_dsv));
        }
        else
        {
            ThrowIfFailed(ctx->m_dev->CreateRenderTargetView(m_tex.Get(), 
                &CD3D11_RENDER_TARGET_VIEW_DESC(m_tex.Get(), D3D11_RTV_DIMENSION_TEXTURE2D), &m_rtv));
            ThrowIfFailed(ctx->m_dev->CreateDepthStencilView(m_depthTex.Get(), 
                &CD3D11_DEPTH_STENCIL_VIEW_DESC(m_depthTex.Get(), D3D11_DSV_DIMENSION_TEXTURE2D), &m_dsv));
        }

        ThrowIfFailed(ctx->m_dev->CreateShaderResourceView(m_tex.Get(), 
            &CD3D11_SHADER_RESOURCE_VIEW_DESC(m_tex.Get(), D3D11_SRV_DIMENSION_TEXTURE2D), &m_srv));
    }

    D3D11TextureR(D3D11Context* ctx, size_t width, size_t height, size_t samples)
        : m_width(width), m_height(height), m_samples(samples) 
    {
        if (samples == 0) m_samples = 1;
        Setup(ctx, width, height, samples);
    }
public:
    size_t samples() const {return m_samples;}
    ComPtr<ID3D11Texture2D> m_tex;
    ComPtr<ID3D11Texture2D> m_msaaTex;
    ComPtr<ID3D11Texture2D> m_depthTex;
    ComPtr<ID3D11RenderTargetView> m_rtv;
    ComPtr<ID3D11DepthStencilView> m_dsv;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    ~D3D11TextureR() = default;

    void resize(D3D11Context* ctx, size_t width, size_t height)
    {
        if (width < 1)
            width = 1;
        if (height < 1)
            height = 1;
        m_width = width;
        m_height = height;
        Setup(ctx, width, height, m_samples);
    }
};

static const size_t SEMANTIC_SIZE_TABLE[] =
{
    12,
    12,
    4,
    8,
    16
};

static const char* SEMANTIC_NAME_TABLE[] =
{
    "POSITION",
    "NORMAL",
    "COLOR",
    "UV",
    "WEIGHT"
};

static const DXGI_FORMAT SEMANTIC_TYPE_TABLE[] =
{
    DXGI_FORMAT_R32G32B32_FLOAT,
    DXGI_FORMAT_R32G32B32_FLOAT,
    DXGI_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT_R32G32_FLOAT,
    DXGI_FORMAT_R32G32B32A32_FLOAT
};

struct D3D11VertexFormat : IVertexFormat
{
    size_t m_elementCount;
    std::unique_ptr<D3D11_INPUT_ELEMENT_DESC[]> m_elements;
    D3D11VertexFormat(size_t elementCount, const VertexElementDescriptor* elements)
        : m_elementCount(elementCount),
        m_elements(new D3D11_INPUT_ELEMENT_DESC[elementCount])
    {
        memset(m_elements.get(), 0, elementCount * sizeof(D3D11_INPUT_ELEMENT_DESC));
        size_t offset = 0;
        for (size_t i=0 ; i<elementCount ; ++i)
        {
            const VertexElementDescriptor* elemin = &elements[i];
            D3D11_INPUT_ELEMENT_DESC& elem = m_elements[i];
            elem.SemanticName = SEMANTIC_NAME_TABLE[elemin->semantic];
            elem.SemanticIndex = elemin->semanticIdx;
            elem.Format = SEMANTIC_TYPE_TABLE[elemin->semantic];
            elem.AlignedByteOffset = offset;
            elem.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
            offset += SEMANTIC_SIZE_TABLE[elemin->semantic];
        }
    }
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
    D3D11_BLEND_INV_DEST_ALPHA
};

class D3D11ShaderPipeline : public IShaderPipeline
{
    friend class D3D11DataFactory;
    D3D11ShaderPipeline(D3D11Context* ctx, ID3DBlob* vert, ID3DBlob* pixel,
        const D3D11VertexFormat* vtxFmt,
        BlendFactor srcFac, BlendFactor dstFac,
        bool depthTest, bool depthWrite, bool backfaceCulling)
    {
        ThrowIfFailed(ctx->m_dev->CreateVertexShader(vert->GetBufferPointer(), vert->GetBufferSize(), nullptr, &m_vShader));
        ThrowIfFailed(ctx->m_dev->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, &m_pShader));

        CD3D11_RASTERIZER_DESC rasDesc(D3D11_FILL_SOLID, backfaceCulling ? D3D11_CULL_BACK : D3D11_CULL_NONE, true, 
            D3D11_DEFAULT_DEPTH_BIAS, D3D11_DEFAULT_DEPTH_BIAS_CLAMP, D3D11_DEFAULT_SLOPE_SCALED_DEPTH_BIAS, 
            true, false, false, false);
        ThrowIfFailed(ctx->m_dev->CreateRasterizerState(&rasDesc, &m_rasState));

        CD3D11_DEPTH_STENCIL_DESC dsDesc(D3D11_DEFAULT);
        dsDesc.DepthEnable = depthTest;
        dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK(depthWrite);
        dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        ThrowIfFailed(ctx->m_dev->CreateDepthStencilState(&dsDesc, &m_dsState));

        CD3D11_BLEND_DESC blDesc(D3D11_DEFAULT);
        blDesc.RenderTarget[0].BlendEnable = (dstFac != BlendFactorZero);
        blDesc.RenderTarget[0].SrcBlend = BLEND_FACTOR_TABLE[srcFac];
        blDesc.RenderTarget[0].DestBlend = BLEND_FACTOR_TABLE[dstFac];
        ThrowIfFailed(ctx->m_dev->CreateBlendState(&blDesc, &m_blState));

        ThrowIfFailed(ctx->m_dev->CreateInputLayout(vtxFmt->m_elements.get(), vtxFmt->m_elementCount, 
            vert->GetBufferPointer(), vert->GetBufferSize(), &m_inLayout));
    }
public:
    ComPtr<ID3D11VertexShader> m_vShader;
    ComPtr<ID3D11PixelShader> m_pShader;
    ComPtr<ID3D11RasterizerState> m_rasState;
    ComPtr<ID3D11DepthStencilState> m_dsState;
    ComPtr<ID3D11BlendState> m_blState;
    ComPtr<ID3D11InputLayout> m_inLayout;
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
    }
};

struct D3D11ShaderDataBinding : IShaderDataBinding
{
    D3D11ShaderPipeline* m_pipeline;
    IGraphicsBuffer* m_vbuf;
    IGraphicsBuffer* m_ibuf;
    size_t m_ubufCount;
    std::unique_ptr<IGraphicsBuffer*[]> m_ubufs;
    size_t m_texCount;
    std::unique_ptr<ITexture*[]> m_texs;
    D3D11ShaderDataBinding(D3D11Context* ctx,
                           IShaderPipeline* pipeline,
                           IGraphicsBuffer* vbuf, IGraphicsBuffer* ibuf,
                           size_t ubufCount, IGraphicsBuffer** ubufs,
                           size_t texCount, ITexture** texs)
    : m_pipeline(static_cast<D3D11ShaderPipeline*>(pipeline)),
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

    void bind(ID3D11DeviceContext* ctx, int b)
    {
        m_pipeline->bind(ctx);

        if (m_vbuf->dynamic())
        {
            D3D11GraphicsBufferD* cbuf = static_cast<D3D11GraphicsBufferD*>(m_vbuf);
            ID3D11Buffer* buf[] = {cbuf->m_bufs[b].Get()};
            UINT strides[] = {UINT(cbuf->m_stride)};
            UINT offsets[] = {0};
            ctx->IASetVertexBuffers(0, 1, buf, strides, offsets);
        }
        else
        {
            D3D11GraphicsBufferS* cbuf = static_cast<D3D11GraphicsBufferS*>(m_vbuf);
            ID3D11Buffer* buf[] = {cbuf->m_buf.Get()};
            UINT strides[] = {UINT(cbuf->m_stride)};
            UINT offsets[] = {0};
            ctx->IASetVertexBuffers(0, 1, buf, strides, offsets);
        }

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

        if (m_ubufCount)
        {
            ID3D11Buffer* constBufs[8];
            for (int i=0 ; i<8 && i<m_ubufCount ; ++i)
            {
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
            ctx->VSSetConstantBuffers(0, m_ubufCount, constBufs);
        }

        if (m_texCount)
        {
            ID3D11ShaderResourceView* srvs[8];
            for (int i=0 ; i<8 && i<m_texCount ; ++i)
            {
                if (m_texs[i]->type() == ITexture::TextureDynamic)
                {
                    D3D11TextureD* ctex = static_cast<D3D11TextureD*>(m_texs[i]);
                    srvs[i] = ctex->m_srvs[b].Get();
                }
                else if (m_texs[i]->type() == ITexture::TextureStatic)
                {
                    D3D11TextureS* ctex = static_cast<D3D11TextureS*>(m_texs[i]);
                    srvs[i] = ctex->m_srv.Get();
                }
            }
            ctx->PSSetShaderResources(0, m_texCount, srvs);
        }
    }
};

struct D3D11CommandQueue : IGraphicsCommandQueue
{
    Platform platform() const {return IGraphicsDataFactory::PlatformD3D11;}
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

    ComPtr<ID3D11CommandList> m_cmdLists[3];
    D3D11TextureR* m_workDoPresent[3];

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

                if (self->m_texResizes.size())
                {
                    for (const auto& resize : self->m_texResizes)
                        resize.first->resize(self->m_ctx, resize.second.first, resize.second.second);
                    self->m_texResizes.clear();
                    self->m_cmdLists[self->m_drawBuf].Reset();
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
                    self->m_cmdLists[self->m_drawBuf].Reset();
                    continue;
                }

                if (self->m_windowCtx->m_needsResize)
                {
                    self->m_windowCtx->m_swapChain->ResizeBuffers(2, self->m_windowCtx->width, self->m_windowCtx->height, 
                        DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
                    self->m_windowCtx->m_needsResize = false;
                    self->m_cmdLists[self->m_drawBuf].Reset();
                    continue;
                }
            }

            ID3D11CommandList* list = self->m_cmdLists[self->m_drawBuf].Get();
            self->m_ctx->m_devCtx->ExecuteCommandList(list, false);
            self->m_cmdLists[self->m_drawBuf].Reset();

            D3D11TextureR* csource = self->m_workDoPresent[self->m_drawBuf];

            if (csource)
            {
                ComPtr<ID3D11Texture2D> dest;
                ThrowIfFailed(self->m_windowCtx->m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &dest));

                if (csource->m_samples > 1)
                {
                    ID3D11Texture2D* src = csource->m_msaaTex.Get();
                    self->m_ctx->m_devCtx->ResolveSubresource(dest.Get(), 0, src, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
                }
                else
                {
                    ID3D11Texture2D* src = csource->m_tex.Get();
                    self->m_ctx->m_devCtx->CopyResource(dest.Get(), src);
                }
                
                self->m_windowCtx->m_swapChain->Present(1, 0);
            }
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

    ~D3D11CommandQueue()
    {
        m_running = false;
        m_cv.notify_one();
        m_thr.join();
    }

    void setShaderDataBinding(IShaderDataBinding* binding)
    {
        D3D11ShaderDataBinding* cbind = static_cast<D3D11ShaderDataBinding*>(binding);
        cbind->bind(m_deferredCtx.Get(), m_fillBuf);

        ID3D11SamplerState* samp[] = {m_ctx->m_ss.Get()};
        m_deferredCtx->PSSetSamplers(0, 1, samp);
    }

    D3D11TextureR* m_boundTarget = nullptr;
    void setRenderTarget(ITextureR* target)
    {
        D3D11TextureR* ctarget = static_cast<D3D11TextureR*>(target);
        ID3D11RenderTargetView* view[] = {ctarget->m_rtv.Get()};
        m_deferredCtx->OMSetRenderTargets(1, view, ctarget->m_dsv.Get());
        m_boundTarget = ctarget;
    }

    void setViewport(const SWindowRect& rect)
    {
        D3D11_VIEWPORT vp = {FLOAT(rect.location[0]), FLOAT(rect.location[1]), FLOAT(rect.size[0]), FLOAT(rect.size[1]), 0.0, 1.0};
        m_deferredCtx->RSSetViewports(1, &vp);
    }

    void flushBufferUpdates() {}

    std::unordered_map<D3D11TextureR*, std::pair<size_t, size_t>> m_texResizes;
    void resizeRenderTexture(ITextureR* tex, size_t width, size_t height)
    {
        D3D11TextureR* ctex = static_cast<D3D11TextureR*>(tex);
        std::unique_lock<std::mutex> lk(m_mt);
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
        if (render)
            m_deferredCtx->ClearRenderTargetView(m_boundTarget->m_rtv.Get(), m_clearColor);
        if (depth)
            m_deferredCtx->ClearDepthStencilView(m_boundTarget->m_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0, 0);
    }

    void setDrawPrimitive(Primitive prim)
    {
        if (prim == PrimitiveTriangles)
            m_deferredCtx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        else if (prim == PrimitiveTriStrips)
            m_deferredCtx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
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

    D3D11TextureR* m_doPresent = nullptr;
    void resolveDisplay(ITextureR* source)
    {
        m_doPresent = static_cast<D3D11TextureR*>(source);
    }

    void execute()
    {
        ThrowIfFailed(m_deferredCtx->FinishCommandList(false, &m_cmdLists[m_fillBuf]));
        m_workDoPresent[m_fillBuf] = m_doPresent;
        m_doPresent = nullptr;
        std::unique_lock<std::mutex> lk(m_mt);
        m_completeBuf = m_fillBuf;
        for (size_t i=0 ; i<3 ; ++i)
        {
            if (i == m_completeBuf || i == m_drawBuf)
                continue;
            m_fillBuf = i;
            break;
        }
        lk.unlock();
        m_cv.notify_one();
    }
};

void D3D11GraphicsBufferD::load(const void* data, size_t sz)
{
    ID3D11Buffer* res = m_bufs[m_q->m_fillBuf].Get();
    D3D11_MAPPED_SUBRESOURCE d;
    m_q->m_deferredCtx->Map(res, 0, D3D11_MAP_WRITE_DISCARD, 0, &d);
    memcpy(d.pData, data, sz);
    m_q->m_deferredCtx->Unmap(res, 0);
}
void* D3D11GraphicsBufferD::map(size_t sz)
{
    ID3D11Buffer* res = m_bufs[m_q->m_fillBuf].Get();
    D3D11_MAPPED_SUBRESOURCE d;
    m_q->m_deferredCtx->Map(res, 0, D3D11_MAP_WRITE_DISCARD, 0, &d);
    return d.pData;
}
void D3D11GraphicsBufferD::unmap()
{
    ID3D11Buffer* res = m_bufs[m_q->m_fillBuf].Get();
    m_q->m_deferredCtx->Unmap(res, 0);
}

void D3D11TextureD::load(const void* data, size_t sz)
{
    ID3D11Texture2D* res = m_texs[m_q->m_fillBuf].Get();
    D3D11_MAPPED_SUBRESOURCE d;
    m_q->m_deferredCtx->Map(res, 0, D3D11_MAP_WRITE_DISCARD, 0, &d);
    memcpy(d.pData, data, sz);
    m_q->m_deferredCtx->Unmap(res, 0);
}
void* D3D11TextureD::map(size_t sz)
{
    ID3D11Texture2D* res = m_texs[m_q->m_fillBuf].Get();
    D3D11_MAPPED_SUBRESOURCE d;
    m_q->m_deferredCtx->Map(res, 0, D3D11_MAP_WRITE_DISCARD, 0, &d);
    return d.pData;
}
void D3D11TextureD::unmap()
{
    ID3D11Texture2D* res = m_texs[m_q->m_fillBuf].Get();
    m_q->m_deferredCtx->Unmap(res, 0);
}

class D3D11DataFactory : public ID3DDataFactory
{
    IGraphicsContext* m_parent;
    IGraphicsData* m_deferredData = nullptr;
    struct D3D11Context* m_ctx;
    std::unordered_set<IGraphicsData*> m_committedData;
public:
    D3D11DataFactory(IGraphicsContext* parent, D3D11Context* ctx)
    : m_parent(parent), m_deferredData(new struct D3D11Data()), m_ctx(ctx)
    {}
    ~D3D11DataFactory() = default;

    Platform platform() const {return PlatformD3D11;}
    const SystemChar* platformName() const {return _S("D3D11");}

    IGraphicsBufferS* newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count)
    {
        D3D11GraphicsBufferS* retval = new D3D11GraphicsBufferS(use, m_ctx, data, stride, count);
        static_cast<D3D11Data*>(m_deferredData)->m_SBufs.emplace_back(retval);
        return retval;
    }

    IGraphicsBufferS* newStaticBuffer(BufferUse use, std::unique_ptr<uint8_t[]>&& data, size_t stride, size_t count)
    {
        std::unique_ptr<uint8_t[]> d = std::move(data);
        D3D11GraphicsBufferS* retval = new D3D11GraphicsBufferS(use, m_ctx, d.get(), stride, count);
        static_cast<D3D11Data*>(m_deferredData)->m_SBufs.emplace_back(retval);
        return retval;
    }

    IGraphicsBufferD* newDynamicBuffer(BufferUse use, size_t stride, size_t count)
    {
        D3D11CommandQueue* q = static_cast<D3D11CommandQueue*>(m_parent->getCommandQueue());
        D3D11GraphicsBufferD* retval = new D3D11GraphicsBufferD(q, use, m_ctx, stride, count);
        static_cast<D3D11Data*>(m_deferredData)->m_DBufs.emplace_back(retval);
        return retval;
    }

    ITextureS* newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
        const void* data, size_t sz)
    {
        D3D11TextureS* retval = new D3D11TextureS(m_ctx, width, height, mips, fmt, data, sz);
        static_cast<D3D11Data*>(m_deferredData)->m_STexs.emplace_back(retval);
        return retval;
    }

    ITextureS* newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                std::unique_ptr<uint8_t[]>&& data, size_t sz)
    {
        std::unique_ptr<uint8_t[]> d = std::move(data);
        D3D11TextureS* retval = new D3D11TextureS(m_ctx, width, height, mips, fmt, d.get(), sz);
        static_cast<D3D11Data*>(m_deferredData)->m_STexs.emplace_back(retval);
        return retval;
    }

    ITextureD* newDynamicTexture(size_t width, size_t height, TextureFormat fmt)
    {
        D3D11CommandQueue* q = static_cast<D3D11CommandQueue*>(m_parent->getCommandQueue());
        D3D11TextureD* retval = new D3D11TextureD(q, m_ctx, width, height, fmt);
        static_cast<D3D11Data*>(m_deferredData)->m_DTexs.emplace_back(retval);
        return retval;
    }

    ITextureR* newRenderTexture(size_t width, size_t height, size_t samples)
    {
        D3D11CommandQueue* q = static_cast<D3D11CommandQueue*>(m_parent->getCommandQueue());
        D3D11TextureR* retval = new D3D11TextureR(m_ctx, width, height, samples);
        static_cast<D3D11Data*>(m_deferredData)->m_RTexs.emplace_back(retval);
        return retval;
    }

    IVertexFormat* newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements)
    {
        D3D11CommandQueue* q = static_cast<D3D11CommandQueue*>(m_parent->getCommandQueue());
        D3D11VertexFormat* retval = new struct D3D11VertexFormat(elementCount, elements);
        static_cast<D3D11Data*>(m_deferredData)->m_VFmts.emplace_back(retval);
        return retval;
    }

#if _DEBUG
#define BOO_D3DCOMPILE_FLAG D3DCOMPILE_DEBUG | D3DCOMPILE_OPTIMIZATION_LEVEL0
#else
#define BOO_D3DCOMPILE_FLAG D3DCOMPILE_OPTIMIZATION_LEVEL3
#endif

    IShaderPipeline* newShaderPipeline
        (const char* vertSource, const char* fragSource,
         ComPtr<ID3DBlob>& vertBlobOut, ComPtr<ID3DBlob>& fragBlobOut,
         ComPtr<ID3DBlob>& pipelineBlob, IVertexFormat* vtxFmt,
         BlendFactor srcFac, BlendFactor dstFac,
         bool depthTest, bool depthWrite, bool backfaceCulling)
    {
        ComPtr<ID3DBlob> errBlob;

        if (!vertBlobOut)
        {
            if (FAILED(D3DCompilePROC(vertSource, strlen(vertSource), "HECL Vert Source", nullptr, nullptr, "main",
                "vs_5_0", BOO_D3DCOMPILE_FLAG, 0, &vertBlobOut, &errBlob)))
            {
                Log.report(LogVisor::FatalError, "error compiling vert shader: %s", errBlob->GetBufferPointer());
                return nullptr;
            }
        }

        if (!fragBlobOut)
        {
            if (FAILED(D3DCompilePROC(fragSource, strlen(fragSource), "HECL Pixel Source", nullptr, nullptr, "main",
                "ps_5_0", BOO_D3DCOMPILE_FLAG, 0, &fragBlobOut, &errBlob)))
            {
                Log.report(LogVisor::FatalError, "error compiling pixel shader: %s", errBlob->GetBufferPointer());
                return nullptr;
            }
        }

        D3D11ShaderPipeline* retval = new D3D11ShaderPipeline(m_ctx, vertBlobOut.Get(), fragBlobOut.Get(),
            static_cast<const D3D11VertexFormat*>(vtxFmt),
            srcFac, dstFac, depthTest, depthWrite, backfaceCulling);
        static_cast<D3D11Data*>(m_deferredData)->m_SPs.emplace_back(retval);
        return retval;
    }

    IShaderDataBinding* newShaderDataBinding(IShaderPipeline* pipeline,
        IVertexFormat* vtxFormat,
        IGraphicsBuffer* vbuf, IGraphicsBuffer* ibuf,
        size_t ubufCount, IGraphicsBuffer** ubufs,
        size_t texCount, ITexture** texs)
    {
        D3D11ShaderDataBinding* retval =
            new D3D11ShaderDataBinding(m_ctx, pipeline, vbuf, ibuf, ubufCount, ubufs, texCount, texs);
        static_cast<D3D11Data*>(m_deferredData)->m_SBinds.emplace_back(retval);
        return retval;
    }

    void reset()
    {
        delete static_cast<D3D11Data*>(m_deferredData);
        m_deferredData = new struct D3D11Data();
    }

    IGraphicsData* commit()
    {
        IGraphicsData* retval = m_deferredData;
        m_deferredData = new struct D3D11Data();
        m_committedData.insert(retval);
        return retval;
    }

    void destroyData(IGraphicsData* d)
    {
        D3D11Data* data = static_cast<D3D11Data*>(d);
        m_committedData.erase(data);
        delete data;
    }

    void destroyAllData()
    {
        for (IGraphicsData* data : m_committedData)
            delete static_cast<D3D11Data*>(data);
        m_committedData.clear();
    }
};


IGraphicsCommandQueue* _NewD3D11CommandQueue(D3D11Context* ctx, D3D11Context::Window* windowCtx, IGraphicsContext* parent)
{
    return new D3D11CommandQueue(ctx, windowCtx, parent);
}

IGraphicsDataFactory* _NewD3D11DataFactory(D3D11Context* ctx, IGraphicsContext* parent)
{
    return new D3D11DataFactory(parent, ctx);
}

}
