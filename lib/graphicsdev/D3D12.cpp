#include "../win/Win32Common.hpp"
#if _WIN32_WINNT_WIN10
#include "logvisor/logvisor.hpp"
#include "boo/graphicsdev/D3D.hpp"
#include "boo/IGraphicsContext.hpp"
#include "Common.hpp"
#include <vector>
#include "d3dx12.h"
#include <d3dcompiler.h>
#include <comdef.h>
#include <algorithm>
#include <mutex>

#define MAX_UNIFORM_COUNT 8
#define MAX_TEXTURE_COUNT 8

#undef min
#undef max

extern PFN_D3D12_SERIALIZE_ROOT_SIGNATURE D3D12SerializeRootSignaturePROC;
extern pD3DCompile D3DCompilePROC;

namespace boo
{
static logvisor::Module Log("boo::D3D12");

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

static inline UINT64 NextHeapOffset(UINT64 offset, const D3D12_RESOURCE_ALLOCATION_INFO& info)
{
    offset += info.SizeInBytes;
    return (offset + info.Alignment - 1) & ~(info.Alignment - 1);
}

struct D3D12Data : IGraphicsDataPriv<D3D12Data>
{
    std::vector<std::unique_ptr<class D3D12ShaderPipeline>> m_SPs;
    std::vector<std::unique_ptr<struct D3D12ShaderDataBinding>> m_SBinds;
    std::vector<std::unique_ptr<class D3D12GraphicsBufferS>> m_SBufs;
    std::vector<std::unique_ptr<class D3D12GraphicsBufferD>> m_DBufs;
    std::vector<std::unique_ptr<class D3D12TextureS>> m_STexs;
    std::vector<std::unique_ptr<class D3D12TextureSA>> m_SATexs;
    std::vector<std::unique_ptr<class D3D12TextureD>> m_DTexs;
    std::vector<std::unique_ptr<class D3D12TextureR>> m_RTexs;
    std::vector<std::unique_ptr<struct D3D12VertexFormat>> m_VFmts;
    ComPtr<ID3D12Heap> m_bufHeap;
    ComPtr<ID3D12Heap> m_texHeap;
};

struct D3D12Pool : IGraphicsBufferPool
{
    struct Buffer
    {
        ComPtr<ID3D12Heap> m_bufHeap;
        std::unique_ptr<class D3D12GraphicsBufferD> m_buf;
        Buffer(ComPtr<ID3D12Heap>&& heap, class D3D12GraphicsBufferD* buf)
        : m_bufHeap(std::move(heap)), m_buf(buf) {}
    };
    std::unordered_map<class D3D12GraphicsBufferD*, Buffer> m_DBufs;
};

static const D3D12_RESOURCE_STATES USE_TABLE[] =
{
    D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
    D3D12_RESOURCE_STATE_INDEX_BUFFER,
    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
};

class D3D12GraphicsBufferS : public IGraphicsBufferS
{
    friend class D3D12DataFactory;
    friend struct D3D12CommandQueue;
    D3D12_RESOURCE_STATES m_state;
    size_t m_sz;
    D3D12_RESOURCE_DESC m_gpuDesc;
    D3D12GraphicsBufferS(BufferUse use, D3D12Context* ctx, const void* data, size_t stride, size_t count)
    : m_state(USE_TABLE[int(use)]), m_stride(stride), m_count(count), m_sz(stride * count)
    {
        m_gpuDesc = CD3DX12_RESOURCE_DESC::Buffer(m_sz);
        size_t reqSz = GetRequiredIntermediateSize(ctx->m_dev.Get(), &m_gpuDesc, 0, 1);
        m_gpuDesc = CD3DX12_RESOURCE_DESC::Buffer(reqSz);
        ThrowIfFailed(ctx->m_dev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE, &m_gpuDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), &m_buf));

        D3D12_SUBRESOURCE_DATA upData = {data, LONG_PTR(m_sz), LONG_PTR(m_sz)};
        if (!PrepSubresources<1>(ctx->m_dev.Get(), &m_gpuDesc, m_buf.Get(), 0, 0, 1, &upData))
            Log.report(logvisor::Fatal, "error preparing resource for upload");
    }
public:
    size_t m_stride;
    size_t m_count;
    ComPtr<ID3D12Resource> m_buf;
    ComPtr<ID3D12Resource> m_gpuBuf;
    ~D3D12GraphicsBufferS() = default;

    UINT64 placeForGPU(D3D12Context* ctx, ID3D12Heap* gpuHeap, UINT64 offset)
    {
        ThrowIfFailed(ctx->m_dev->CreatePlacedResource(gpuHeap, offset, &m_gpuDesc, m_state,
            nullptr, __uuidof(ID3D12Resource), &m_gpuBuf));

        /* Stage resource upload */
        CommandSubresourcesTransfer<1>(ctx->m_dev.Get(), ctx->m_loadlist.Get(), m_gpuBuf.Get(), m_buf.Get(), 0, 0, 1);
        ctx->m_loadlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_gpuBuf.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

        return NextHeapOffset(offset, ctx->m_dev->GetResourceAllocationInfo(0, 1, &m_gpuDesc));
    }
};

class D3D12GraphicsBufferD : public IGraphicsBufferD
{
    friend class D3D12DataFactory;
    friend struct D3D12CommandQueue;
    D3D12CommandQueue* m_q;
    D3D12_RESOURCE_STATES m_state;
    std::unique_ptr<uint8_t[]> m_cpuBuf;
    size_t m_cpuSz;
    int m_validSlots = 0;
    D3D12GraphicsBufferD(D3D12CommandQueue* q, BufferUse use, D3D12Context* ctx, size_t stride, size_t count)
    : m_state(USE_TABLE[int(use)]), m_q(q), m_stride(stride), m_count(count)
    {
        m_cpuSz = stride * count;
        m_cpuBuf.reset(new uint8_t[m_cpuSz]);
        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(m_cpuSz);
        size_t reqSz = GetRequiredIntermediateSize(ctx->m_dev.Get(), &desc, 0, 1);
        desc = CD3DX12_RESOURCE_DESC::Buffer(reqSz);
        for (int i=0 ; i<2 ; ++i)
        {
            ThrowIfFailed(ctx->m_dev->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), &m_bufs[i]));
        }
    }
    void update(int b);
public:
    size_t m_stride;
    size_t m_count;
    ComPtr<ID3D12Resource> m_bufs[2];
    ComPtr<ID3D12Resource> m_gpuBufs[2];
    ~D3D12GraphicsBufferD() = default;

    void load(const void* data, size_t sz);
    void* map(size_t sz);
    void unmap();

    UINT64 placeForGPU(D3D12Context* ctx, ID3D12Heap* gpuHeap, UINT64 offset)
    {
        for (int i=0 ; i<2 ; ++i)
        {
            D3D12_RESOURCE_DESC desc = m_bufs[i]->GetDesc();
            ThrowIfFailed(ctx->m_dev->CreatePlacedResource(gpuHeap, offset, &desc, m_state,
                nullptr, __uuidof(ID3D12Resource), &m_gpuBufs[i]));
            offset = NextHeapOffset(offset, ctx->m_dev->GetResourceAllocationInfo(0, 1, &desc));
        }
        return offset;
    }
};

class D3D12TextureS : public ITextureS
{
    friend class D3D12DataFactory;
    TextureFormat m_fmt;
    size_t m_sz;
    D3D12_RESOURCE_DESC m_gpuDesc;
    D3D12TextureS(D3D12Context* ctx, size_t width, size_t height, size_t mips,
                  TextureFormat fmt, const void* data, size_t sz)
    : m_fmt(fmt), m_sz(sz), m_mipCount(mips)
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


        m_gpuDesc = CD3DX12_RESOURCE_DESC::Tex2D(pfmt, width, height, 1, mips);
        size_t reqSz = GetRequiredIntermediateSize(ctx->m_dev.Get(), &m_gpuDesc, 0, mips);
        ThrowIfFailed(ctx->m_dev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(reqSz),
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), &m_tex));

        const uint8_t* dataIt = static_cast<const uint8_t*>(data);
        D3D12_SUBRESOURCE_DATA upData[16] = {};
        for (size_t i=0 ; i<m_gpuDesc.MipLevels && i<16 ; ++i)
        {
            upData[i].pData = dataIt;
            upData[i].RowPitch = width * pxPitchNum / pxPitchDenom;
            upData[i].SlicePitch = upData[i].RowPitch * height;
            dataIt += upData[i].SlicePitch;
            width /= 2;
            height /= 2;
        }

        if (!PrepSubresources<16>(ctx->m_dev.Get(), &m_gpuDesc, m_tex.Get(), 0, 0, m_gpuDesc.MipLevels, upData))
            Log.report(logvisor::Fatal, "error preparing resource for upload");
    }
public:
    size_t m_mipCount;
    ComPtr<ID3D12Resource> m_tex;
    ComPtr<ID3D12Resource> m_gpuTex;
    ~D3D12TextureS() = default;

    UINT64 placeForGPU(D3D12Context* ctx, ID3D12Heap* gpuHeap, UINT64 offset)
    {
        ThrowIfFailed(ctx->m_dev->CreatePlacedResource(gpuHeap, offset, &m_gpuDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, __uuidof(ID3D12Resource), &m_gpuTex));

        CommandSubresourcesTransfer<16>(ctx->m_dev.Get(), ctx->m_loadlist.Get(), m_gpuTex.Get(),
                                        m_tex.Get(), 0, 0, m_gpuDesc.MipLevels);
        ctx->m_loadlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_gpuTex.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

        return NextHeapOffset(offset, ctx->m_dev->GetResourceAllocationInfo(0, 1, &m_gpuDesc));
    }

    TextureFormat format() const {return m_fmt;}
};

class D3D12TextureSA : public ITextureSA
{
    friend class D3D12DataFactory;
    TextureFormat m_fmt;
    size_t m_layers;
    size_t m_sz;
    D3D12_RESOURCE_DESC m_gpuDesc;
    D3D12TextureSA(D3D12Context* ctx, size_t width, size_t height, size_t layers,
                   TextureFormat fmt, const void* data, size_t sz)
    : m_fmt(fmt), m_layers(layers), m_sz(sz)
    {
        size_t pxPitch;
        DXGI_FORMAT pixelFmt;
        switch (fmt)
        {
        case TextureFormat::RGBA8:
            pxPitch = 4;
            pixelFmt = DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
        case TextureFormat::I8:
            pxPitch = 1;
            pixelFmt = DXGI_FORMAT_R8_UNORM;
            break;
        default:
            Log.report(logvisor::Fatal, "unsupported tex format");
        }

        m_gpuDesc = CD3DX12_RESOURCE_DESC::Tex2D(pixelFmt, width, height, layers, 1);
        size_t reqSz = GetRequiredIntermediateSize(ctx->m_dev.Get(), &m_gpuDesc, 0, layers);
        ThrowIfFailed(ctx->m_dev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(reqSz),
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), &m_tex));

        const uint8_t* dataIt = static_cast<const uint8_t*>(data);

        if (layers > 16)
        {
            std::unique_ptr<D3D12_SUBRESOURCE_DATA[]> upData(new D3D12_SUBRESOURCE_DATA[layers]);
            for (size_t i=0 ; i<layers ; ++i)
            {
                upData[i].pData = dataIt;
                upData[i].RowPitch = width * pxPitch;
                upData[i].SlicePitch = upData[i].RowPitch * height;
                dataIt += upData[i].SlicePitch;
            }
            if (!PrepSubresources(ctx->m_dev.Get(), &m_gpuDesc, m_tex.Get(), 0, 0, layers, upData.get()))
                Log.report(logvisor::Fatal, "error preparing resource for upload");
        }
        else
        {
            D3D12_SUBRESOURCE_DATA upData[16] = {};
            for (size_t i=0 ; i<layers ; ++i)
            {
                upData[i].pData = dataIt;
                upData[i].RowPitch = width * pxPitch;
                upData[i].SlicePitch = upData[i].RowPitch * height;
                dataIt += upData[i].SlicePitch;
            }
            if (!PrepSubresources<16>(ctx->m_dev.Get(), &m_gpuDesc, m_tex.Get(), 0, 0, layers, upData))
                Log.report(logvisor::Fatal, "error preparing resource for upload");
        }
    }
public:
    ComPtr<ID3D12Resource> m_tex;
    ComPtr<ID3D12Resource> m_gpuTex;
    ~D3D12TextureSA() = default;

    UINT64 placeForGPU(D3D12Context* ctx, ID3D12Heap* gpuHeap, UINT64 offset)
    {
        ThrowIfFailed(ctx->m_dev->CreatePlacedResource(gpuHeap, offset, &m_gpuDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, __uuidof(ID3D12Resource), &m_gpuTex));

        if (m_gpuDesc.DepthOrArraySize > 16)
        {
            CommandSubresourcesTransfer(ctx->m_dev.Get(), ctx->m_loadlist.Get(), m_gpuTex.Get(), m_tex.Get(), 0, 0, m_gpuDesc.DepthOrArraySize);
            ctx->m_loadlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_gpuTex.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
        }
        else
        {
            CommandSubresourcesTransfer<16>(ctx->m_dev.Get(), ctx->m_loadlist.Get(), m_gpuTex.Get(), m_tex.Get(), 0, 0, m_gpuDesc.DepthOrArraySize);
            ctx->m_loadlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_gpuTex.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
        }

        return NextHeapOffset(offset, ctx->m_dev->GetResourceAllocationInfo(0, 1, &m_gpuDesc));
    }

    TextureFormat format() const {return m_fmt;}
    size_t layers() const {return m_layers;}
};

class D3D12TextureD : public ITextureD
{
    friend class D3D12DataFactory;
    friend struct D3D12CommandQueue;
    size_t m_width = 0;
    size_t m_height = 0;
    TextureFormat m_fmt;
    D3D12CommandQueue* m_q;
    D3D12_RESOURCE_DESC m_gpuDesc;
    std::unique_ptr<uint8_t[]> m_cpuBuf;
    size_t m_rowPitch;
    size_t m_cpuSz;
    int m_validSlots = 0;
    D3D12TextureD(D3D12CommandQueue* q, D3D12Context* ctx, size_t width, size_t height, TextureFormat fmt)
    : m_width(width), m_height(height), m_fmt(fmt), m_q(q)
    {
        DXGI_FORMAT pixelFmt;
        size_t pxPitch;
        switch (fmt)
        {
        case TextureFormat::RGBA8:
            pixelFmt = DXGI_FORMAT_R8G8B8A8_UNORM;
            pxPitch = 4;
            break;
        case TextureFormat::I8:
            pixelFmt = DXGI_FORMAT_R8_UNORM;
            pxPitch = 1;
            break;
        default:
            Log.report(logvisor::Fatal, "unsupported tex format");
        }

        m_cpuSz = width * height * pxPitch;
        m_rowPitch = width * pxPitch;
        m_cpuBuf.reset(new uint8_t[m_cpuSz]);

        m_gpuDesc = CD3DX12_RESOURCE_DESC::Tex2D(pixelFmt, width, height);
        size_t reqSz = GetRequiredIntermediateSize(ctx->m_dev.Get(), &m_gpuDesc, 0, 1);
        for (int i=0 ; i<2 ; ++i)
        {
            ThrowIfFailed(ctx->m_dev->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(reqSz),
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), &m_texs[i]));
        }
    }
    void update(int b);
public:
    ComPtr<ID3D12Resource> m_texs[2];
    ComPtr<ID3D12Resource> m_gpuTexs[2];
    ~D3D12TextureD() = default;

    void load(const void* data, size_t sz);
    void* map(size_t sz);
    void unmap();

    UINT64 placeForGPU(D3D12Context* ctx, ID3D12Heap* gpuHeap, UINT64 offset)
    {
        for (int i=0 ; i<2 ; ++i)
        {
            ThrowIfFailed(ctx->m_dev->CreatePlacedResource(gpuHeap, offset, &m_gpuDesc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                nullptr, __uuidof(ID3D12Resource), &m_gpuTexs[i]));
            offset = NextHeapOffset(offset, ctx->m_dev->GetResourceAllocationInfo(0, 1, &m_gpuDesc));
        }
        return offset;
    }

    TextureFormat format() const {return m_fmt;}
};

static const float BLACK_COLOR[] = {0.0,0.0,0.0,1.0};

class D3D12TextureR : public ITextureR
{
    friend class D3D12DataFactory;
    friend struct D3D12CommandQueue;
    size_t m_width = 0;
    size_t m_height = 0;
    size_t m_samples = 0;
    bool m_enableShaderColorBind;
    bool m_enableShaderDepthBind;

    void Setup(D3D12Context* ctx, size_t width, size_t height, size_t samples,
               bool enableShaderColorBind, bool enableShaderDepthBind)
    {
        D3D12_DESCRIPTOR_HEAP_DESC rtvdesc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1};
        ThrowIfFailed(ctx->m_dev->CreateDescriptorHeap(&rtvdesc, __uuidof(ID3D12DescriptorHeap), &m_rtvHeap));

        D3D12_DESCRIPTOR_HEAP_DESC dsvdesc = {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1};
        ThrowIfFailed(ctx->m_dev->CreateDescriptorHeap(&dsvdesc, __uuidof(ID3D12DescriptorHeap), &m_dsvHeap));

        D3D12_RTV_DIMENSION rtvDim;
        D3D12_DSV_DIMENSION dsvDim;
        CD3DX12_RESOURCE_DESC rtvresdesc;
        CD3DX12_RESOURCE_DESC dsvresdesc;
        CD3DX12_RESOURCE_DESC cbindresdesc;
        CD3DX12_RESOURCE_DESC dbindresdesc;

        if (samples > 1)
        {
            rtvDim = D3D12_RTV_DIMENSION_TEXTURE2DMS;
            dsvDim = D3D12_DSV_DIMENSION_TEXTURE2DMS;
            rtvresdesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 0, samples,
                0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT);
            dsvresdesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D24_UNORM_S8_UINT, width, height, 1, 0, samples,
                0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT);
            cbindresdesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 0, samples,
                0, D3D12_RESOURCE_FLAG_NONE, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT);
            dbindresdesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D24_UNORM_S8_UINT, width, height, 1, 0, samples,
                0, D3D12_RESOURCE_FLAG_NONE, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT);
        }
        else
        {
            rtvDim = D3D12_RTV_DIMENSION_TEXTURE2D;
            dsvDim = D3D12_DSV_DIMENSION_TEXTURE2D;
            rtvresdesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 0, 1,
                0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
            dsvresdesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D24_UNORM_S8_UINT, width, height, 1, 0, 1,
                0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
            cbindresdesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 0, 1,
                0, D3D12_RESOURCE_FLAG_NONE);
            dbindresdesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D24_UNORM_S8_UINT, width, height, 1, 0, 1,
                0, D3D12_RESOURCE_FLAG_NONE);
        }

        ThrowIfFailed(ctx->m_dev->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
            &rtvresdesc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
            __uuidof(ID3D12Resource), &m_colorTex));

        ThrowIfFailed(ctx->m_dev->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
            &dsvresdesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr,
            __uuidof(ID3D12Resource), &m_depthTex));

        D3D12_RENDER_TARGET_VIEW_DESC rtvvdesc = {DXGI_FORMAT_R8G8B8A8_UNORM, rtvDim};
        ctx->m_dev->CreateRenderTargetView(m_colorTex.Get(), &rtvvdesc, m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvvdesc = {DXGI_FORMAT_D24_UNORM_S8_UINT, dsvDim};
        ctx->m_dev->CreateDepthStencilView(m_depthTex.Get(), &dsvvdesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

        if (enableShaderColorBind)
        {
            ThrowIfFailed(ctx->m_dev->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
                &cbindresdesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                __uuidof(ID3D12Resource), &m_colorBindTex));
        }

        if (enableShaderDepthBind)
        {
            ThrowIfFailed(ctx->m_dev->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
                &dbindresdesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                __uuidof(ID3D12Resource), &m_depthBindTex));
        }
    }

    D3D12CommandQueue* m_q;
    D3D12TextureR(D3D12Context* ctx, D3D12CommandQueue* q, size_t width, size_t height, size_t samples,
                  bool enableShaderColorBind, bool enableShaderDepthBind)
    : m_q(q), m_width(width), m_height(height), m_samples(samples),
      m_enableShaderColorBind(enableShaderColorBind), m_enableShaderDepthBind(enableShaderDepthBind)
    {
        if (samples == 0) m_samples = 1;
        Setup(ctx, width, height, samples, enableShaderColorBind, enableShaderDepthBind);
    }
public:
    size_t samples() const {return m_samples;}
    ComPtr<ID3D12Resource> m_colorTex;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;

    ComPtr<ID3D12Resource> m_depthTex;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;

    ComPtr<ID3D12Resource> m_colorBindTex;

    ComPtr<ID3D12Resource> m_depthBindTex;

    ~D3D12TextureR();

    void resize(D3D12Context* ctx, size_t width, size_t height)
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

struct D3D12VertexFormat : IVertexFormat
{
    size_t m_elementCount;
    std::unique_ptr<D3D12_INPUT_ELEMENT_DESC[]> m_elements;
    size_t m_stride = 0;
    size_t m_instStride = 0;

    D3D12VertexFormat(size_t elementCount, const VertexElementDescriptor* elements)
    : m_elementCount(elementCount),
      m_elements(new D3D12_INPUT_ELEMENT_DESC[elementCount])
    {
        memset(m_elements.get(), 0, elementCount * sizeof(D3D12_INPUT_ELEMENT_DESC));
        for (size_t i=0 ; i<elementCount ; ++i)
        {
            const VertexElementDescriptor* elemin = &elements[i];
            D3D12_INPUT_ELEMENT_DESC& elem = m_elements[i];
            int semantic = int(elemin->semantic & boo::VertexSemantic::SemanticMask);
            elem.SemanticName = SEMANTIC_NAME_TABLE[semantic];
            elem.SemanticIndex = elemin->semanticIdx;
            elem.Format = SEMANTIC_TYPE_TABLE[semantic];
            if ((elemin->semantic & boo::VertexSemantic::Instanced) != boo::VertexSemantic::None)
            {
                elem.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
                elem.InstanceDataStepRate = 1;
                elem.InputSlot = 1;
                elem.AlignedByteOffset = m_instStride;
                m_instStride += SEMANTIC_SIZE_TABLE[semantic];
            }
            else
            {
                elem.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                elem.AlignedByteOffset = m_stride;
                m_stride += SEMANTIC_SIZE_TABLE[semantic];
            }
        }
    }
};

static const D3D12_PRIMITIVE_TOPOLOGY PRIMITIVE_TABLE[] =
{
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
};

static const D3D12_BLEND BLEND_FACTOR_TABLE[] =
{
    D3D12_BLEND_ZERO,
    D3D12_BLEND_ONE,
    D3D12_BLEND_SRC_COLOR,
    D3D12_BLEND_INV_SRC_COLOR,
    D3D12_BLEND_DEST_COLOR,
    D3D12_BLEND_INV_DEST_COLOR,
    D3D12_BLEND_SRC_ALPHA,
    D3D12_BLEND_INV_SRC_ALPHA,
    D3D12_BLEND_DEST_ALPHA,
    D3D12_BLEND_INV_DEST_ALPHA,
    D3D12_BLEND_SRC1_COLOR,
    D3D12_BLEND_INV_SRC1_COLOR
};

class D3D12ShaderPipeline : public IShaderPipeline
{
    friend class D3D12DataFactory;
    friend struct D3D12ShaderDataBinding;
    const D3D12VertexFormat* m_vtxFmt;

    D3D12ShaderPipeline(D3D12Context* ctx, ID3DBlob* vert, ID3DBlob* pixel, ID3DBlob* pipeline,
                        const D3D12VertexFormat* vtxFmt,
                        BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                        bool depthTest, bool depthWrite, bool backfaceCulling)
    : m_vtxFmt(vtxFmt), m_topology(PRIMITIVE_TABLE[int(prim)])
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = ctx->m_rs.Get();
        desc.VS = {vert->GetBufferPointer(), vert->GetBufferSize()};
        desc.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        if (dstFac != BlendFactor::Zero)
        {
            desc.BlendState.RenderTarget[0].BlendEnable = true;
            desc.BlendState.RenderTarget[0].SrcBlend = BLEND_FACTOR_TABLE[int(srcFac)];
            desc.BlendState.RenderTarget[0].DestBlend = BLEND_FACTOR_TABLE[int(dstFac)];
        }
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.FrontCounterClockwise = TRUE;
        if (!backfaceCulling)
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        if (!depthTest)
            desc.DepthStencilState.DepthEnable = false;
        if (!depthWrite)
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.InputLayout.NumElements = vtxFmt->m_elementCount;
        desc.InputLayout.pInputElementDescs = vtxFmt->m_elements.get();
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        desc.SampleDesc.Count = 1;
        if (pipeline)
        {
            desc.CachedPSO.pCachedBlob = pipeline->GetBufferPointer();
            desc.CachedPSO.CachedBlobSizeInBytes = pipeline->GetBufferSize();
        }
        ThrowIfFailed(ctx->m_dev->CreateGraphicsPipelineState(&desc, __uuidof(ID3D12PipelineState), &m_state));
    }
public:
    ComPtr<ID3D12PipelineState> m_state;
    D3D12_PRIMITIVE_TOPOLOGY m_topology;
    ~D3D12ShaderPipeline() = default;
    D3D12ShaderPipeline& operator=(const D3D12ShaderPipeline&) = delete;
    D3D12ShaderPipeline(const D3D12ShaderPipeline&) = delete;
};

static UINT64 PlaceBufferForGPU(IGraphicsBuffer* buf, D3D12Context* ctx, ID3D12Heap* gpuHeap, UINT64 offset)
{
    if (buf->dynamic())
        return static_cast<D3D12GraphicsBufferD*>(buf)->placeForGPU(ctx, gpuHeap, offset);
    else
        return static_cast<D3D12GraphicsBufferS*>(buf)->placeForGPU(ctx, gpuHeap, offset);
}

static UINT64 PlaceTextureForGPU(ITexture* tex, D3D12Context* ctx, ID3D12Heap* gpuHeap, UINT64 offset)
{
    switch (tex->type())
    {
    case TextureType::Dynamic:
        return static_cast<D3D12TextureD*>(tex)->placeForGPU(ctx, gpuHeap, offset);
    case TextureType::Static:
        return static_cast<D3D12TextureS*>(tex)->placeForGPU(ctx, gpuHeap, offset);
    case TextureType::StaticArray:
        return static_cast<D3D12TextureSA*>(tex)->placeForGPU(ctx, gpuHeap, offset);
    }
    return offset;
}

static ID3D12Resource* GetBufferGPUResource(const IGraphicsBuffer* buf, int idx,
                                            D3D12_VERTEX_BUFFER_VIEW& descOut, size_t offset)
{
    if (buf->dynamic())
    {
        const D3D12GraphicsBufferD* cbuf = static_cast<const D3D12GraphicsBufferD*>(buf);
        descOut.SizeInBytes = cbuf->m_count * cbuf->m_stride - offset;
        descOut.StrideInBytes = cbuf->m_stride;
        descOut.BufferLocation = cbuf->m_gpuBufs[idx]->GetGPUVirtualAddress() + offset;
        return cbuf->m_gpuBufs[idx].Get();
    }
    else
    {
        const D3D12GraphicsBufferS* cbuf = static_cast<const D3D12GraphicsBufferS*>(buf);
        descOut.SizeInBytes = cbuf->m_count * cbuf->m_stride - offset;
        descOut.StrideInBytes = cbuf->m_stride;
        descOut.BufferLocation = cbuf->m_gpuBuf->GetGPUVirtualAddress() + offset;
        return cbuf->m_gpuBuf.Get();
    }
}

static ID3D12Resource* GetBufferGPUResource(const IGraphicsBuffer* buf, int idx,
                                            D3D12_INDEX_BUFFER_VIEW& descOut)
{
    if (buf->dynamic())
    {
        const D3D12GraphicsBufferD* cbuf = static_cast<const D3D12GraphicsBufferD*>(buf);
        descOut.SizeInBytes = cbuf->m_count * cbuf->m_stride;
        descOut.BufferLocation = cbuf->m_gpuBufs[idx]->GetGPUVirtualAddress();
        descOut.Format = DXGI_FORMAT_R32_UINT;
        return cbuf->m_gpuBufs[idx].Get();
    }
    else
    {
        const D3D12GraphicsBufferS* cbuf = static_cast<const D3D12GraphicsBufferS*>(buf);
        descOut.SizeInBytes = cbuf->m_count * cbuf->m_stride;
        descOut.BufferLocation = cbuf->m_gpuBuf->GetGPUVirtualAddress();
        descOut.Format = DXGI_FORMAT_R32_UINT;
        return cbuf->m_gpuBuf.Get();
    }
}

static ID3D12Resource* GetBufferGPUResource(const IGraphicsBuffer* buf, int idx,
                                            D3D12_CONSTANT_BUFFER_VIEW_DESC& descOut)
{
    if (buf->dynamic())
    {
        const D3D12GraphicsBufferD* cbuf = static_cast<const D3D12GraphicsBufferD*>(buf);
        descOut.SizeInBytes = cbuf->m_count * cbuf->m_stride;
        descOut.BufferLocation = cbuf->m_gpuBufs[idx]->GetGPUVirtualAddress();
        return cbuf->m_gpuBufs[idx].Get();
    }
    else
    {
        const D3D12GraphicsBufferS* cbuf = static_cast<const D3D12GraphicsBufferS*>(buf);
        descOut.SizeInBytes = cbuf->m_count * cbuf->m_stride;
        descOut.BufferLocation = cbuf->m_gpuBuf->GetGPUVirtualAddress();
        return cbuf->m_gpuBuf.Get();
    }
}

static const struct RGBATex2DNoMipViewDesc : D3D12_SHADER_RESOURCE_VIEW_DESC
{
    RGBATex2DNoMipViewDesc()
    {
        Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        Texture2D = {UINT(0), UINT(1), UINT(0), 0.0f};
    }
} RGBATex2DNoMipViewDesc;

static const struct RGBATex2DViewDesc : D3D12_SHADER_RESOURCE_VIEW_DESC
{
    RGBATex2DViewDesc()
    {
        Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        Texture2D = {UINT(0), UINT(-1), UINT(0), 0.0f};
    }
} RGBATex2DViewDesc;

static const struct DXTTex2DViewDesc : D3D12_SHADER_RESOURCE_VIEW_DESC
{
    DXTTex2DViewDesc()
    {
        Format = DXGI_FORMAT_BC1_UNORM;
        ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        Texture2D = {UINT(0), UINT(-1), UINT(0), 0.0f};
    }
} DXTTex2DViewDesc;

static const struct GreyTex2DViewDesc : D3D12_SHADER_RESOURCE_VIEW_DESC
{
    GreyTex2DViewDesc()
    {
        Format = DXGI_FORMAT_R8_UNORM;
        ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        Texture2D = {UINT(0), UINT(-1), UINT(0), 0.0f};
    }
} GreyTex2DViewDesc;

static const struct RGBATex2DArrayViewDesc : D3D12_SHADER_RESOURCE_VIEW_DESC
{
    RGBATex2DArrayViewDesc()
    {
        Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        Texture2DArray = {UINT(0), UINT(1), 0, 0, UINT(0), 0.0f};
    }
} RGBATex2DArrayViewDesc;

static const struct GreyTex2DArrayViewDesc : D3D12_SHADER_RESOURCE_VIEW_DESC
{
    GreyTex2DArrayViewDesc()
    {
        Format = DXGI_FORMAT_R8_UNORM;
        ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        Texture2DArray = {UINT(0), UINT(1), 0, 0, UINT(0), 0.0f};
    }
} GreyTex2DArrayViewDesc;

static ID3D12Resource* GetTextureGPUResource(const ITexture* tex, int idx,
                                             D3D12_SHADER_RESOURCE_VIEW_DESC& descOut)
{
    switch (tex->type())
    {
    case TextureType::Dynamic:
    {
        const D3D12TextureD* ctex = static_cast<const D3D12TextureD*>(tex);
        switch (ctex->format())
        {
        case TextureFormat::RGBA8:
            descOut = RGBATex2DViewDesc;
            break;
        case TextureFormat::I8:
            descOut = GreyTex2DViewDesc;
            break;
        default:break;
        }
        descOut.Texture2D.MipLevels = 1;
        return ctex->m_gpuTexs[idx].Get();
    }
    case TextureType::Static:
    {
        const D3D12TextureS* ctex = static_cast<const D3D12TextureS*>(tex);
        switch (ctex->format())
        {
        case TextureFormat::RGBA8:
            descOut = RGBATex2DViewDesc;
            break;
        case TextureFormat::I8:
            descOut = GreyTex2DViewDesc;
            break;
        case TextureFormat::DXT1:
            descOut = DXTTex2DViewDesc;
            break;
        default:break;
        }
        descOut.Texture2D.MipLevels = ctex->m_mipCount;
        return ctex->m_gpuTex.Get();
    }
    case TextureType::StaticArray:
    {
        const D3D12TextureSA* ctex = static_cast<const D3D12TextureSA*>(tex);
        switch (ctex->format())
        {
        case TextureFormat::RGBA8:
            descOut = RGBATex2DArrayViewDesc;
            break;
        case TextureFormat::I8:
            descOut = GreyTex2DArrayViewDesc;
            break;
        default:break;
        }
        descOut.Texture2DArray.ArraySize = ctex->layers();
        return ctex->m_gpuTex.Get();
    }
    case TextureType::Render:
    {
        const D3D12TextureR* ctex = static_cast<const D3D12TextureR*>(tex);
        descOut = RGBATex2DNoMipViewDesc;
        return ctex->m_colorBindTex.Get();
    }
    default: break;
    }
    return nullptr;
}

struct D3D12ShaderDataBinding : IShaderDataBindingPriv<D3D12Data>
{
    D3D12ShaderPipeline* m_pipeline;
    ComPtr<ID3D12Heap> m_gpuHeap;
    ComPtr<ID3D12DescriptorHeap> m_descHeap[2];
    IGraphicsBuffer* m_vbuf;
    IGraphicsBuffer* m_instVbuf;
    IGraphicsBuffer* m_ibuf;
    size_t m_ubufCount;
    std::unique_ptr<IGraphicsBuffer*[]> m_ubufs;
    std::vector<std::pair<size_t,size_t>> m_ubufOffs;
    size_t m_texCount;
    ID3D12Resource* m_knownViewHandles[2][8] = {};
    std::unique_ptr<ITexture*[]> m_texs;
    D3D12_VERTEX_BUFFER_VIEW m_vboView[2][2] = {{},{}};
    D3D12_INDEX_BUFFER_VIEW m_iboView[2];
    size_t m_vertOffset, m_instOffset;

    D3D12ShaderDataBinding(D3D12Data* d,
                           D3D12Context* ctx,
                           IShaderPipeline* pipeline,
                           IGraphicsBuffer* vbuf, IGraphicsBuffer* instVbuf, IGraphicsBuffer* ibuf,
                           size_t ubufCount, IGraphicsBuffer** ubufs,
                           const size_t* ubufOffs, const size_t* ubufSizes,
                           size_t texCount, ITexture** texs,
                           size_t baseVert, size_t baseInst)
    : IShaderDataBindingPriv(d),
      m_pipeline(static_cast<D3D12ShaderPipeline*>(pipeline)),
      m_vbuf(vbuf),
      m_instVbuf(instVbuf),
      m_ibuf(ibuf),
      m_ubufCount(ubufCount),
      m_ubufs(new IGraphicsBuffer*[ubufCount]),
      m_texCount(texCount),
      m_texs(new ITexture*[texCount])
    {
        m_vertOffset = baseVert * m_pipeline->m_vtxFmt->m_stride;
        m_instOffset = baseInst * m_pipeline->m_vtxFmt->m_instStride;

        if (ubufOffs && ubufSizes)
        {
            m_ubufOffs.reserve(ubufCount);
            for (size_t i=0 ; i<ubufCount ; ++i)
            {
#ifndef NDEBUG
                if (ubufOffs[i] % 256)
                    Log.report(logvisor::Fatal, "non-256-byte-aligned uniform-offset %d provided to newShaderDataBinding", int(i));
#endif
                m_ubufOffs.emplace_back(ubufOffs[i], (ubufSizes[i] + 255) & ~255);
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

    void commit(D3D12Context* ctx)
    {
        /* Create double-buffered descriptor heaps */
        D3D12_DESCRIPTOR_HEAP_DESC desc;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = MAX_UNIFORM_COUNT + MAX_TEXTURE_COUNT;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        desc.NodeMask = 0;

        UINT incSz = ctx->m_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (int b=0 ; b<2 ; ++b)
        {
            ThrowIfFailed(ctx->m_dev->CreateDescriptorHeap(&desc, _uuidof(ID3D12DescriptorHeap), &m_descHeap[b]));
            CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_descHeap[b]->GetCPUDescriptorHandleForHeapStart());

            if (m_vbuf)
                GetBufferGPUResource(m_vbuf, b, m_vboView[b][0], m_vertOffset);
            if (m_instVbuf)
                GetBufferGPUResource(m_instVbuf, b, m_vboView[b][1], m_instOffset);
            if (m_ibuf)
                GetBufferGPUResource(m_ibuf, b, m_iboView[b]);
            if (m_ubufOffs.size())
            {
                for (size_t i=0 ; i<MAX_UNIFORM_COUNT ; ++i)
                {
                    if (i<m_ubufCount)
                    {
                        const std::pair<size_t,size_t>& offPair = m_ubufOffs[i];
                        D3D12_CONSTANT_BUFFER_VIEW_DESC viewDesc;
                        GetBufferGPUResource(m_ubufs[i], b, viewDesc);
                        viewDesc.BufferLocation += offPair.first;
                        viewDesc.SizeInBytes = (offPair.second + 255) & ~255;
                        ctx->m_dev->CreateConstantBufferView(&viewDesc, handle);
                    }
                    handle.Offset(1, incSz);
                }
            }
            else
            {
                for (size_t i=0 ; i<MAX_UNIFORM_COUNT ; ++i)
                {
                    if (i<m_ubufCount)
                    {
                        D3D12_CONSTANT_BUFFER_VIEW_DESC viewDesc;
                        GetBufferGPUResource(m_ubufs[i], b, viewDesc);
                        viewDesc.SizeInBytes = (viewDesc.SizeInBytes + 255) & ~255;
                        ctx->m_dev->CreateConstantBufferView(&viewDesc, handle);
                    }
                    handle.Offset(1, incSz);
                }
            }
            for (size_t i=0 ; i<MAX_TEXTURE_COUNT ; ++i)
            {
                if (i<m_texCount && m_texs[i])
                {
                    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
                    ID3D12Resource* res = GetTextureGPUResource(m_texs[i], b, srvDesc);
                    m_knownViewHandles[b][i] = res;
                    ctx->m_dev->CreateShaderResourceView(res, &srvDesc, handle);
                }
                handle.Offset(1, incSz);
            }
        }
    }

    void bind(D3D12Context* ctx, ID3D12GraphicsCommandList* list, int b)
    {
        UINT incSz = UINT(-1);
        CD3DX12_CPU_DESCRIPTOR_HANDLE heapStart;
        for (size_t i=0 ; i<MAX_TEXTURE_COUNT ; ++i)
        {
            if (i<m_texCount && m_texs[i])
            {
                if (m_texs[i]->type() == TextureType::Render)
                {
                    const D3D12TextureR* ctex = static_cast<const D3D12TextureR*>(m_texs[i]);
                    ID3D12Resource* res = ctex->m_colorBindTex.Get();
                    if (res != m_knownViewHandles[b][i])
                    {
                        if (incSz == UINT(-1))
                        {
                            incSz = ctx->m_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                            heapStart = m_descHeap[b]->GetCPUDescriptorHandleForHeapStart();
                        }
                        m_knownViewHandles[b][i] = res;
                        ctx->m_dev->CreateShaderResourceView(res, &RGBATex2DNoMipViewDesc,
                            CD3DX12_CPU_DESCRIPTOR_HANDLE(heapStart, MAX_UNIFORM_COUNT + i, incSz));
                    }
                }
            }
        }

        ID3D12DescriptorHeap* heap[] = {m_descHeap[b].Get()};
        list->SetDescriptorHeaps(1, heap);
        list->SetGraphicsRootDescriptorTable(0, m_descHeap[b]->GetGPUDescriptorHandleForHeapStart());
        list->SetPipelineState(m_pipeline->m_state.Get());
        list->IASetVertexBuffers(0, 2, m_vboView[b]);
        if (m_ibuf)
            list->IASetIndexBuffer(&m_iboView[b]);
        list->IASetPrimitiveTopology(m_pipeline->m_topology);
    }
};

static ID3D12GraphicsCommandList* WaitForLoadList(D3D12Context* ctx)
{
    /* Wait for previous transaction to complete (if in progress) */
    if (ctx->m_loadfence->GetCompletedValue() < ctx->m_loadfenceval)
    {
        ThrowIfFailed(ctx->m_loadfence->SetEventOnCompletion(ctx->m_loadfenceval, ctx->m_loadfencehandle));
        WaitForSingleObject(ctx->m_loadfencehandle, INFINITE);
    }
    return ctx->m_loadlist.Get();
}

struct D3D12CommandQueue : IGraphicsCommandQueue
{
    Platform platform() const {return IGraphicsDataFactory::Platform::D3D12;}
    const SystemChar* platformName() const {return _S("D3D12");}
    D3D12Context* m_ctx;
    D3D12Context::Window* m_windowCtx;
    IGraphicsContext* m_parent;
    ComPtr<ID3D12GraphicsCommandList> m_cmdList;
    ComPtr<ID3D12Fence> m_fence;

    ComPtr<ID3D12CommandAllocator> m_dynamicCmdAlloc[2];
    ComPtr<ID3D12CommandQueue> m_dynamicCmdQueue;
    ComPtr<ID3D12GraphicsCommandList> m_dynamicCmdList;
    UINT64 m_dynamicBufFenceVal = 0;
    ComPtr<ID3D12Fence> m_dynamicBufFence;
    HANDLE m_dynamicBufFenceHandle;
    bool m_dynamicNeedsReset = false;

    HANDLE m_renderFenceHandle;
    bool m_running = true;

    size_t m_fillBuf = 0;
    size_t m_drawBuf = 0;

    void resetCommandList()
    {
        ThrowIfFailed(m_ctx->m_qalloc[m_fillBuf]->Reset());
        ThrowIfFailed(m_cmdList->Reset(m_ctx->m_qalloc[m_fillBuf].Get(), nullptr));
        m_cmdList->SetGraphicsRootSignature(m_ctx->m_rs.Get());
    }

    void resetDynamicCommandList()
    {
        ThrowIfFailed(m_dynamicCmdAlloc[m_fillBuf]->Reset());
        ThrowIfFailed(m_dynamicCmdList->Reset(m_dynamicCmdAlloc[m_fillBuf].Get(), nullptr));
        m_dynamicNeedsReset = false;
    }

    void stallDynamicUpload()
    {
        if (m_dynamicNeedsReset)
        {
            if (m_dynamicBufFence->GetCompletedValue() < m_dynamicBufFenceVal)
            {
                ThrowIfFailed(m_dynamicBufFence->SetEventOnCompletion(m_dynamicBufFenceVal,
                                                                      m_dynamicBufFenceHandle));
                WaitForSingleObject(m_dynamicBufFenceHandle, INFINITE);
            }
            resetDynamicCommandList();
        }
    }

    D3D12CommandQueue(D3D12Context* ctx, D3D12Context::Window* windowCtx, IGraphicsContext* parent,
                      ID3D12CommandQueue** cmdQueueOut)
    : m_ctx(ctx), m_windowCtx(windowCtx), m_parent(parent)
    {
        ThrowIfFailed(ctx->m_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                         __uuidof(ID3D12CommandAllocator),
                                                         &ctx->m_qalloc[0]));
        ThrowIfFailed(ctx->m_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                         __uuidof(ID3D12CommandAllocator),
                                                         &ctx->m_qalloc[1]));
        D3D12_COMMAND_QUEUE_DESC desc =
        {
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            D3D12_COMMAND_QUEUE_PRIORITY_HIGH,
            D3D12_COMMAND_QUEUE_FLAG_NONE
        };
        ThrowIfFailed(ctx->m_dev->CreateCommandQueue(&desc, __uuidof(ID3D12CommandQueue), &ctx->m_q));
        *cmdQueueOut = ctx->m_q.Get();
        ThrowIfFailed(ctx->m_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), &m_fence));
        ThrowIfFailed(ctx->m_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ctx->m_qalloc[0].Get(),
                                                    nullptr, __uuidof(ID3D12GraphicsCommandList), &m_cmdList));
        m_renderFenceHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        m_cmdList->SetGraphicsRootSignature(m_ctx->m_rs.Get());

        ThrowIfFailed(ctx->m_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), &m_dynamicBufFence));
        m_dynamicBufFenceHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        ThrowIfFailed(ctx->m_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                         __uuidof(ID3D12CommandAllocator), &m_dynamicCmdAlloc[0]));
        ThrowIfFailed(ctx->m_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                         __uuidof(ID3D12CommandAllocator), &m_dynamicCmdAlloc[1]));
        ThrowIfFailed(ctx->m_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_dynamicCmdAlloc[0].Get(),
                                                    nullptr, __uuidof(ID3D12GraphicsCommandList), &m_dynamicCmdList));
    }

    void stopRenderer()
    {
        m_running = false;
        if (m_fence->GetCompletedValue() < m_submittedFenceVal)
        {
            ThrowIfFailed(m_fence->SetEventOnCompletion(m_submittedFenceVal, m_renderFenceHandle));
            WaitForSingleObject(m_renderFenceHandle, INFINITE);
        }
    }

    ~D3D12CommandQueue()
    {
        if (m_running) stopRenderer();
    }

    void setShaderDataBinding(IShaderDataBinding* binding)
    {
        D3D12ShaderDataBinding* cbind = static_cast<D3D12ShaderDataBinding*>(binding);
        cbind->bind(m_ctx, m_cmdList.Get(), m_fillBuf);
    }

    D3D12TextureR* m_boundTarget = nullptr;
    void setRenderTarget(ITextureR* target)
    {
        D3D12TextureR* ctarget = static_cast<D3D12TextureR*>(target);

        m_cmdList->OMSetRenderTargets(1, &ctarget->m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
                                      false, &ctarget->m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

        m_boundTarget = ctarget;
    }

    void setViewport(const SWindowRect& rect, float znear, float zfar)
    {
        D3D12_VIEWPORT vp = {FLOAT(rect.location[0]), FLOAT(m_boundTarget->m_height - rect.location[1] - rect.size[1]),
                             FLOAT(rect.size[0]), FLOAT(rect.size[1]), znear, zfar};
        m_cmdList->RSSetViewports(1, &vp);
    }

    void setScissor(const SWindowRect& rect)
    {
        if (m_boundTarget)
        {
            D3D12_RECT d3drect = {LONG(rect.location[0]), LONG(m_boundTarget->m_height - rect.location[1] - rect.size[1]),
                                  LONG(rect.location[0] + rect.size[0]), LONG(m_boundTarget->m_height - rect.location[1])};
            m_cmdList->RSSetScissorRects(1, &d3drect);
        }
    }

    std::unordered_map<D3D12TextureR*, std::pair<size_t, size_t>> m_texResizes;
    void resizeRenderTexture(ITextureR* tex, size_t width, size_t height)
    {
        D3D12TextureR* ctex = static_cast<D3D12TextureR*>(tex);
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
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_boundTarget->m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
            m_cmdList->ClearRenderTargetView(handle, m_clearColor, 0, nullptr);
        }
        if (depth)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_boundTarget->m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
            m_cmdList->ClearDepthStencilView(handle, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
        }
    }

    void draw(size_t start, size_t count)
    {
        m_cmdList->DrawInstanced(count, 1, start, 0);
    }

    void drawIndexed(size_t start, size_t count)
    {
        m_cmdList->DrawIndexedInstanced(count, 1, start, 0, 0);
    }

    void drawInstances(size_t start, size_t count, size_t instCount)
    {
        m_cmdList->DrawInstanced(count, instCount, start, 0);
    }

    void drawInstancesIndexed(size_t start, size_t count, size_t instCount)
    {
        m_cmdList->DrawIndexedInstanced(count, instCount, start, 0, 0);
    }

    void resolveBindTexture(ITextureR* texture, const SWindowRect& rect, bool tlOrigin, bool color, bool depth)
    {
        const D3D12TextureR* tex = static_cast<const D3D12TextureR*>(texture);
        if (color && tex->m_enableShaderColorBind)
        {
            D3D12_RESOURCE_BARRIER copySetup[] =
            {
                CD3DX12_RESOURCE_BARRIER::Transition(tex->m_colorTex.Get(),
                    D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(tex->m_colorBindTex.Get(),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)
            };
            m_cmdList->ResourceBarrier(2, copySetup);

            if (tex->m_samples > 1)
            {
                m_cmdList->CopyResource(tex->m_colorBindTex.Get(), tex->m_colorTex.Get());
            }
            else
            {
                SWindowRect intersectRect = rect.intersect(SWindowRect(0, 0, tex->m_width, tex->m_height));
                int y = tlOrigin ? intersectRect.location[1] : (tex->m_height - intersectRect.size[1] - intersectRect.location[1]);
                D3D12_BOX box = {UINT(intersectRect.location[0]), UINT(y), 0,
                                 UINT(intersectRect.location[0] + intersectRect.size[0]), UINT(y + intersectRect.size[1]), 1};
                D3D12_TEXTURE_COPY_LOCATION dst = {tex->m_colorBindTex.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
                dst.SubresourceIndex = 0;
                D3D12_TEXTURE_COPY_LOCATION src = {tex->m_colorTex.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
                src.SubresourceIndex = 0;
                m_cmdList->CopyTextureRegion(&dst, box.left, box.top, 0, &src, &box);
            }

            D3D12_RESOURCE_BARRIER copyTeardown[] =
            {
                CD3DX12_RESOURCE_BARRIER::Transition(tex->m_colorTex.Get(),
                    D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
                CD3DX12_RESOURCE_BARRIER::Transition(tex->m_colorBindTex.Get(),
                    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
            };
            m_cmdList->ResourceBarrier(2, copyTeardown);
        }
        if (depth && tex->m_enableShaderDepthBind)
        {
            D3D12_RESOURCE_BARRIER copySetup[] =
            {
                CD3DX12_RESOURCE_BARRIER::Transition(tex->m_depthTex.Get(),
                    D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(tex->m_depthBindTex.Get(),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)
            };
            m_cmdList->ResourceBarrier(2, copySetup);

            m_cmdList->CopyResource(tex->m_depthBindTex.Get(), tex->m_depthTex.Get());

            D3D12_RESOURCE_BARRIER copyTeardown[] =
            {
                CD3DX12_RESOURCE_BARRIER::Transition(tex->m_depthTex.Get(),
                    D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE),
                CD3DX12_RESOURCE_BARRIER::Transition(tex->m_depthBindTex.Get(),
                    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
            };
            m_cmdList->ResourceBarrier(2, copyTeardown);
        }
    }

    bool m_doPresent = false;
    void resolveDisplay(ITextureR* source)
    {
        D3D12TextureR* csource = static_cast<D3D12TextureR*>(source);

        if (m_windowCtx->m_needsResize)
        {
            UINT nodeMasks[] = {0,0};
            IUnknown* const queues[] = {m_ctx->m_q.Get(), m_ctx->m_q.Get()};
            m_windowCtx->m_swapChain->ResizeBuffers1(2, m_windowCtx->width, m_windowCtx->height,
                DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH, nodeMasks, queues);
            m_windowCtx->m_backBuf = m_windowCtx->m_swapChain->GetCurrentBackBufferIndex();
            m_windowCtx->m_needsResize = false;
        }

        ComPtr<ID3D12Resource> dest;
        ThrowIfFailed(m_windowCtx->m_swapChain->GetBuffer(m_windowCtx->m_backBuf, __uuidof(ID3D12Resource), &dest));

        ID3D12Resource* src = csource->m_colorTex.Get();
        D3D12_RESOURCE_STATES srcState = D3D12_RESOURCE_STATE_COPY_SOURCE;
        if (m_boundTarget == csource)
            srcState = D3D12_RESOURCE_STATE_RENDER_TARGET;

        if (csource->m_samples > 1)
        {
            D3D12_RESOURCE_BARRIER msaaSetup[] =
            {
                CD3DX12_RESOURCE_BARRIER::Transition(src,
                    srcState, D3D12_RESOURCE_STATE_RESOLVE_SOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(dest.Get(),
                    D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RESOLVE_DEST)
            };
            m_cmdList->ResourceBarrier(2, msaaSetup);

            m_cmdList->ResolveSubresource(dest.Get(), 0, src, 0, DXGI_FORMAT_R8G8B8A8_UNORM);

            D3D12_RESOURCE_BARRIER msaaTeardown[] =
            {
                CD3DX12_RESOURCE_BARRIER::Transition(src,
                    D3D12_RESOURCE_STATE_RESOLVE_SOURCE, srcState),
                CD3DX12_RESOURCE_BARRIER::Transition(dest.Get(),
                    D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PRESENT)
            };
            m_cmdList->ResourceBarrier(2, msaaTeardown);
        }
        else
        {
            if (srcState != D3D12_RESOURCE_STATE_COPY_SOURCE)
            {
                D3D12_RESOURCE_BARRIER copySetup[] =
                {
                    CD3DX12_RESOURCE_BARRIER::Transition(src,
                        srcState, D3D12_RESOURCE_STATE_COPY_SOURCE),
                    CD3DX12_RESOURCE_BARRIER::Transition(dest.Get(),
                        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST)
                };
                m_cmdList->ResourceBarrier(2, copySetup);
            }
            else
            {
                D3D12_RESOURCE_BARRIER copySetup[] =
                {
                    CD3DX12_RESOURCE_BARRIER::Transition(dest.Get(),
                        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST)
                };
                m_cmdList->ResourceBarrier(1, copySetup);
            }

            m_cmdList->CopyResource(dest.Get(), src);

            if (srcState != D3D12_RESOURCE_STATE_COPY_SOURCE)
            {
                D3D12_RESOURCE_BARRIER copyTeardown[] =
                {
                    CD3DX12_RESOURCE_BARRIER::Transition(src,
                        D3D12_RESOURCE_STATE_COPY_SOURCE, srcState),
                    CD3DX12_RESOURCE_BARRIER::Transition(dest.Get(),
                        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT)
                };
                m_cmdList->ResourceBarrier(2, copyTeardown);
            }
            else
            {
                D3D12_RESOURCE_BARRIER copyTeardown[] =
                {
                    CD3DX12_RESOURCE_BARRIER::Transition(dest.Get(),
                        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT)
                };
                m_cmdList->ResourceBarrier(1, copyTeardown);
            }
        }
        m_doPresent = true;
    }

    UINT64 m_submittedFenceVal = 0;
    void execute();
};

D3D12TextureR::~D3D12TextureR()
{
    if (m_q->m_boundTarget == this)
        m_q->m_boundTarget = nullptr;
}

void D3D12GraphicsBufferD::update(int b)
{
    int slot = 1 << b;
    if ((slot & m_validSlots) == 0)
    {
        m_q->stallDynamicUpload();
        ID3D12Resource* res = m_bufs[b].Get();
        ID3D12Resource* gpuRes = m_gpuBufs[b].Get();
        D3D12_SUBRESOURCE_DATA d = {m_cpuBuf.get(), LONG_PTR(m_cpuSz), LONG_PTR(m_cpuSz)};
        m_q->m_dynamicCmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(gpuRes,
            m_state, D3D12_RESOURCE_STATE_COPY_DEST));
        if (!UpdateSubresources<1>(m_q->m_dynamicCmdList.Get(), gpuRes, res, 0, 0, 1, &d))
            Log.report(logvisor::Fatal, "unable to update dynamic buffer data");
        m_q->m_dynamicCmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(gpuRes,
            D3D12_RESOURCE_STATE_COPY_DEST, m_state));
        m_validSlots |= slot;
    }
}

void D3D12GraphicsBufferD::load(const void* data, size_t sz)
{
    size_t bufSz = std::min(sz, m_cpuSz);
    memcpy(m_cpuBuf.get(), data, bufSz);
    m_validSlots = 0;
}
void* D3D12GraphicsBufferD::map(size_t sz)
{
    if (sz > m_cpuSz)
        return nullptr;
    return m_cpuBuf.get();
}
void D3D12GraphicsBufferD::unmap()
{
    m_validSlots = 0;
}

void D3D12TextureD::update(int b)
{
    int slot = 1 << b;
    if ((slot & m_validSlots) == 0)
    {
        m_q->stallDynamicUpload();
        ID3D12Resource* res = m_texs[b].Get();
        ID3D12Resource* gpuRes = m_gpuTexs[b].Get();
        D3D12_SUBRESOURCE_DATA d = {m_cpuBuf.get(), LONG_PTR(m_rowPitch), LONG_PTR(m_cpuSz)};
        D3D12_RESOURCE_BARRIER setupCopy[] =
        {
            CD3DX12_RESOURCE_BARRIER::Transition(gpuRes,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
        };
        m_q->m_dynamicCmdList->ResourceBarrier(1, setupCopy);
        if (!UpdateSubresources<1>(m_q->m_dynamicCmdList.Get(), gpuRes, res, 0, 0, 1, &d))
            Log.report(logvisor::Fatal, "unable to update dynamic texture data");
        D3D12_RESOURCE_BARRIER teardownCopy[] =
        {
            CD3DX12_RESOURCE_BARRIER::Transition(gpuRes,
                        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        };
        m_q->m_dynamicCmdList->ResourceBarrier(1, teardownCopy);
        m_validSlots |= slot;
    }
}
void D3D12TextureD::load(const void* data, size_t sz)
{
    size_t bufSz = std::min(sz, m_cpuSz);
    memcpy(m_cpuBuf.get(), data, bufSz);
    m_validSlots = 0;
}
void* D3D12TextureD::map(size_t sz)
{
    if (sz > m_cpuSz)
        return nullptr;
    return m_cpuBuf.get();
}
void D3D12TextureD::unmap()
{
    m_validSlots = 0;
}

class D3D12DataFactory : public ID3DDataFactory
{
    friend struct D3D12CommandQueue;
    IGraphicsContext* m_parent;
    static thread_local D3D12Data* m_deferredData;
    struct D3D12Context* m_ctx;
    std::unordered_set<D3D12Data*> m_committedData;
    std::unordered_set<D3D12Pool*> m_committedPools;
    std::mutex m_committedMutex;
    uint32_t m_sampleCount;

    void destroyData(IGraphicsData* d)
    {
        std::unique_lock<std::mutex> lk(m_committedMutex);
        D3D12Data* data = static_cast<D3D12Data*>(d);
        m_committedData.erase(data);
        data->decrement();
    }

    void destroyAllData()
    {
        std::unique_lock<std::mutex> lk(m_committedMutex);
        for (D3D12Data* data : m_committedData)
            data->decrement();
        for (IGraphicsBufferPool* pool : m_committedPools)
            delete static_cast<D3D12Pool*>(pool);
        m_committedData.clear();
        m_committedPools.clear();
    }

    void destroyPool(IGraphicsBufferPool* p)
    {
        std::unique_lock<std::mutex> lk(m_committedMutex);
        D3D12Pool* pool = static_cast<D3D12Pool*>(p);
        m_committedPools.erase(pool);
        delete pool;
    }

    IGraphicsBufferD* newPoolBuffer(IGraphicsBufferPool* p, BufferUse use,
                                    size_t stride, size_t count)
    {
        D3D12CommandQueue* q = static_cast<D3D12CommandQueue*>(m_parent->getCommandQueue());
        D3D12Pool* pool = static_cast<D3D12Pool*>(p);
        D3D12GraphicsBufferD* retval = new D3D12GraphicsBufferD(q, use, m_ctx, stride, count);

        /* Gather resource descriptions */
        D3D12_RESOURCE_DESC bufDescs[2];
        bufDescs[0] = retval->m_bufs[0]->GetDesc();
        bufDescs[1] = retval->m_bufs[1]->GetDesc();

        /* Create heap */
        ComPtr<ID3D12Heap> bufHeap;
        D3D12_RESOURCE_ALLOCATION_INFO bufAllocInfo =
            m_ctx->m_dev->GetResourceAllocationInfo(0, 2, bufDescs);
        ThrowIfFailed(m_ctx->m_dev->CreateHeap(&CD3DX12_HEAP_DESC(bufAllocInfo,
            D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS),
            __uuidof(ID3D12Heap), &bufHeap));

        /* Place resources */
        PlaceBufferForGPU(retval, m_ctx, bufHeap.Get(), 0);

        pool->m_DBufs.emplace(std::make_pair(retval, D3D12Pool::Buffer{std::move(bufHeap), retval}));
        return retval;
    }

    void deletePoolBuffer(IGraphicsBufferPool* p, IGraphicsBufferD* buf)
    {
        D3D12Pool* pool = static_cast<D3D12Pool*>(p);
        pool->m_DBufs.erase(static_cast<D3D12GraphicsBufferD*>(buf));
    }

public:
    D3D12DataFactory(IGraphicsContext* parent, D3D12Context* ctx, uint32_t sampleCount)
    : m_parent(parent), m_ctx(ctx), m_sampleCount(sampleCount)
    {
        CD3DX12_DESCRIPTOR_RANGE ranges[] =
        {
            {D3D12_DESCRIPTOR_RANGE_TYPE_CBV, MAX_UNIFORM_COUNT, 0},
            {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, MAX_TEXTURE_COUNT, 0}
        };
        CD3DX12_ROOT_PARAMETER rootParms[1];
        rootParms[0].InitAsDescriptorTable(2, ranges);

        ComPtr<ID3DBlob> rsOutBlob;
        ComPtr<ID3DBlob> rsErrorBlob;
        ThrowIfFailed(D3D12SerializeRootSignaturePROC(
            &CD3DX12_ROOT_SIGNATURE_DESC(1, rootParms, 1, &CD3DX12_STATIC_SAMPLER_DESC(0),
                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT),
            D3D_ROOT_SIGNATURE_VERSION_1, &rsOutBlob, &rsErrorBlob));

        ThrowIfFailed(ctx->m_dev->CreateRootSignature(0, rsOutBlob->GetBufferPointer(),
            rsOutBlob->GetBufferSize(), __uuidof(ID3D12RootSignature), &ctx->m_rs));
    }
    ~D3D12DataFactory() {destroyAllData();}

    Platform platform() const {return Platform::D3D12;}
    const SystemChar* platformName() const {return _S("D3D12");}

    class Context : public ID3DDataFactory::Context
    {
        friend class D3D12DataFactory;
        D3D12DataFactory& m_parent;
        Context(D3D12DataFactory& parent) : m_parent(parent) {}
    public:
        Platform platform() const {return Platform::D3D12;}
        const SystemChar* platformName() const {return _S("D3D12");}

        IGraphicsBufferS* newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count)
        {
            D3D12GraphicsBufferS* retval = new D3D12GraphicsBufferS(use, m_parent.m_ctx, data, stride, count);
            static_cast<D3D12Data*>(m_deferredData)->m_SBufs.emplace_back(retval);
            return retval;
        }

        IGraphicsBufferD* newDynamicBuffer(BufferUse use, size_t stride, size_t count)
        {
            D3D12CommandQueue* q = static_cast<D3D12CommandQueue*>(m_parent.m_parent->getCommandQueue());
            D3D12GraphicsBufferD* retval = new D3D12GraphicsBufferD(q, use, m_parent.m_ctx, stride, count);
            static_cast<D3D12Data*>(m_deferredData)->m_DBufs.emplace_back(retval);
            return retval;
        }

        ITextureS* newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                    const void* data, size_t sz)
        {
            D3D12TextureS* retval = new D3D12TextureS(m_parent.m_ctx, width, height, mips, fmt, data, sz);
            static_cast<D3D12Data*>(m_deferredData)->m_STexs.emplace_back(retval);
            return retval;
        }

        ITextureSA* newStaticArrayTexture(size_t width, size_t height, size_t layers, TextureFormat fmt,
                                          const void* data, size_t sz)
        {
            D3D12TextureSA* retval = new D3D12TextureSA(m_parent.m_ctx, width, height, layers, fmt, data, sz);
            static_cast<D3D12Data*>(m_deferredData)->m_SATexs.emplace_back(retval);
            return retval;
        }

        ITextureD* newDynamicTexture(size_t width, size_t height, TextureFormat fmt)
        {
            D3D12CommandQueue* q = static_cast<D3D12CommandQueue*>(m_parent.m_parent->getCommandQueue());
            D3D12TextureD* retval = new D3D12TextureD(q, m_parent.m_ctx, width, height, fmt);
            static_cast<D3D12Data*>(m_deferredData)->m_DTexs.emplace_back(retval);
            return retval;
        }

        ITextureR* newRenderTexture(size_t width, size_t height,
                                    bool enableShaderColorBind, bool enableShaderDepthBind)
        {
            D3D12CommandQueue* q = static_cast<D3D12CommandQueue*>(m_parent.m_parent->getCommandQueue());
            D3D12TextureR* retval = new D3D12TextureR(m_parent.m_ctx, q, width, height, m_parent.m_sampleCount,
                                                      enableShaderColorBind, enableShaderDepthBind);
            static_cast<D3D12Data*>(m_deferredData)->m_RTexs.emplace_back(retval);
            return retval;
        }

        IVertexFormat* newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements,
                                       size_t baseVert, size_t baseInst)
        {
            D3D12VertexFormat* retval = new struct D3D12VertexFormat(elementCount, elements);
            static_cast<D3D12Data*>(m_deferredData)->m_VFmts.emplace_back(retval);
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
             BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
             bool depthTest, bool depthWrite, bool backfaceCulling)
        {
            ComPtr<ID3DBlob> errBlob;

            //printf("%s\n", vertSource);
            //printf("%s\n", fragSource);

            if (!vertBlobOut)
            {
                if (FAILED(D3DCompilePROC(vertSource, strlen(vertSource), "HECL Vert Source", nullptr, nullptr, "main",
                    "vs_5_0", BOO_D3DCOMPILE_FLAG, 0, &vertBlobOut, &errBlob)))
                {
                    Log.report(logvisor::Fatal, "error compiling vert shader: %s", errBlob->GetBufferPointer());
                    return nullptr;
                }
            }

            if (!fragBlobOut)
            {
                if (FAILED(D3DCompilePROC(fragSource, strlen(fragSource), "HECL Pixel Source", nullptr, nullptr, "main",
                    "ps_5_0", BOO_D3DCOMPILE_FLAG, 0, &fragBlobOut, &errBlob)))
                {
                    Log.report(logvisor::Fatal, "error compiling pixel shader: %s", errBlob->GetBufferPointer());
                    return nullptr;
                }
            }

            D3D12ShaderPipeline* retval = new D3D12ShaderPipeline(m_parent.m_ctx, vertBlobOut.Get(), fragBlobOut.Get(), pipelineBlob.Get(),
                                                                  static_cast<const D3D12VertexFormat*>(vtxFmt),
                                                                  srcFac, dstFac, prim, depthTest, depthWrite, backfaceCulling);
            if (!pipelineBlob)
                retval->m_state->GetCachedBlob(&pipelineBlob);
            static_cast<D3D12Data*>(m_deferredData)->m_SPs.emplace_back(retval);
            return retval;
        }

        IShaderDataBinding* newShaderDataBinding(IShaderPipeline* pipeline,
                IVertexFormat* vtxFormat,
                IGraphicsBuffer* vbuf, IGraphicsBuffer* instVbuf, IGraphicsBuffer* ibuf,
                size_t ubufCount, IGraphicsBuffer** ubufs, const PipelineStage* ubufStages,
                const size_t* ubufOffs, const size_t* ubufSizes,
                size_t texCount, ITexture** texs,
                size_t baseVert, size_t baseInst)
        {
            D3D12Data* d = static_cast<D3D12Data*>(m_deferredData);
            D3D12ShaderDataBinding* retval =
                new D3D12ShaderDataBinding(d, m_parent.m_ctx, pipeline, vbuf, instVbuf, ibuf,
                                           ubufCount, ubufs, ubufOffs, ubufSizes, texCount, texs,
                                           baseVert, baseInst);
            d->m_SBinds.emplace_back(retval);
            return retval;
        }
    };

    GraphicsDataToken commitTransaction(const FactoryCommitFunc& trans)
    {
        if (m_deferredData)
            Log.report(logvisor::Fatal, "nested commitTransaction usage detected");
        m_deferredData = new D3D12Data();

        D3D12DataFactory::Context ctx(*this);
        if (!trans(ctx))
        {
            delete m_deferredData;
            m_deferredData = nullptr;
            return GraphicsDataToken(this, nullptr);
        }

        D3D12Data* retval = static_cast<D3D12Data*>(m_deferredData);

        /* Gather resource descriptions */
        std::vector<D3D12_RESOURCE_DESC> bufDescs;
        bufDescs.reserve(retval->m_SBufs.size() + retval->m_DBufs.size() * 2);

        std::vector<D3D12_RESOURCE_DESC> texDescs;
        texDescs.reserve(retval->m_STexs.size() + retval->m_SATexs.size() + retval->m_DTexs.size() * 2);

        for (std::unique_ptr<D3D12GraphicsBufferS>& buf : retval->m_SBufs)
            bufDescs.push_back(buf->m_gpuDesc);

        for (std::unique_ptr<D3D12GraphicsBufferD>& buf : retval->m_DBufs)
        {
            bufDescs.push_back(buf->m_bufs[0]->GetDesc());
            bufDescs.push_back(buf->m_bufs[1]->GetDesc());
        }

        for (std::unique_ptr<D3D12TextureS>& tex : retval->m_STexs)
            texDescs.push_back(tex->m_gpuDesc);

        for (std::unique_ptr<D3D12TextureSA>& tex : retval->m_SATexs)
            texDescs.push_back(tex->m_gpuDesc);

        for (std::unique_ptr<D3D12TextureD>& tex : retval->m_DTexs)
        {
            texDescs.push_back(tex->m_gpuDesc);
            texDescs.push_back(tex->m_gpuDesc);
        }

        /* Create heap */
        if (bufDescs.size())
        {
            D3D12_RESOURCE_ALLOCATION_INFO bufAllocInfo =
                m_ctx->m_dev->GetResourceAllocationInfo(0, bufDescs.size(), bufDescs.data());
            ThrowIfFailed(m_ctx->m_dev->CreateHeap(&CD3DX12_HEAP_DESC(bufAllocInfo,
                D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS),
                __uuidof(ID3D12Heap), &retval->m_bufHeap));
        }
        if (texDescs.size())
        {
            D3D12_RESOURCE_ALLOCATION_INFO texAllocInfo =
                m_ctx->m_dev->GetResourceAllocationInfo(0, texDescs.size(), texDescs.data());
            ThrowIfFailed(m_ctx->m_dev->CreateHeap(&CD3DX12_HEAP_DESC(texAllocInfo,
                D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES),
                __uuidof(ID3D12Heap), &retval->m_texHeap));
        }
        ID3D12Heap* bufHeap = retval->m_bufHeap.Get();
        ID3D12Heap* texHeap = retval->m_texHeap.Get();

        /* Place resources */
        UINT64 offsetBuf = 0;
        for (std::unique_ptr<D3D12GraphicsBufferS>& buf : retval->m_SBufs)
            offsetBuf = PlaceBufferForGPU(buf.get(), m_ctx, bufHeap, offsetBuf);

        for (std::unique_ptr<D3D12GraphicsBufferD>& buf : retval->m_DBufs)
            offsetBuf = PlaceBufferForGPU(buf.get(), m_ctx, bufHeap, offsetBuf);

        UINT64 offsetTex = 0;
        for (std::unique_ptr<D3D12TextureS>& tex : retval->m_STexs)
            offsetTex = PlaceTextureForGPU(tex.get(), m_ctx, texHeap, offsetTex);

        for (std::unique_ptr<D3D12TextureSA>& tex : retval->m_SATexs)
            offsetTex = PlaceTextureForGPU(tex.get(), m_ctx, texHeap, offsetTex);

        for (std::unique_ptr<D3D12TextureD>& tex : retval->m_DTexs)
            offsetTex = PlaceTextureForGPU(tex.get(), m_ctx, texHeap, offsetTex);

        /* Execute static uploads */
        ThrowIfFailed(m_ctx->m_loadlist->Close());
        ID3D12CommandList* list[] = {m_ctx->m_loadlist.Get()};
        m_ctx->m_loadq->ExecuteCommandLists(1, list);
        ++m_ctx->m_loadfenceval;
        ThrowIfFailed(m_ctx->m_loadq->Signal(m_ctx->m_loadfence.Get(), m_ctx->m_loadfenceval));

        /* Commit data bindings (create descriptor heaps) */
        for (std::unique_ptr<D3D12ShaderDataBinding>& bind : retval->m_SBinds)
            bind->commit(m_ctx);

        /* Block handle return until data is ready on GPU */
        WaitForLoadList(m_ctx);

        /* Reset allocator and list */
        ThrowIfFailed(m_ctx->m_loadqalloc->Reset());
        ThrowIfFailed(m_ctx->m_loadlist->Reset(m_ctx->m_loadqalloc.Get(), nullptr));

        /* Delete static upload heaps */
        for (std::unique_ptr<D3D12GraphicsBufferS>& buf : retval->m_SBufs)
            buf->m_buf.Reset();

        for (std::unique_ptr<D3D12TextureS>& tex : retval->m_STexs)
            tex->m_tex.Reset();

        for (std::unique_ptr<D3D12TextureSA>& tex : retval->m_SATexs)
            tex->m_tex.Reset();

        /* All set! */
        std::unique_lock<std::mutex> lk(m_committedMutex);
        m_deferredData = nullptr;
        m_committedData.insert(retval);
        lk.unlock();
        return GraphicsDataToken(this, retval);
    }

    GraphicsBufferPoolToken newBufferPool()
    {
        std::unique_lock<std::mutex> lk(m_committedMutex);
        D3D12Pool* retval = new D3D12Pool;
        m_committedPools.insert(retval);
        return GraphicsBufferPoolToken(this, retval);
    }
};

thread_local D3D12Data* D3D12DataFactory::m_deferredData;

void D3D12CommandQueue::execute()
{
    if (!m_running)
        return;

    /* Stage dynamic uploads */
    D3D12DataFactory* gfxF = static_cast<D3D12DataFactory*>(m_parent->getDataFactory());
    std::unique_lock<std::mutex> datalk(gfxF->m_committedMutex);
    for (D3D12Data* d : gfxF->m_committedData)
    {
        for (std::unique_ptr<D3D12GraphicsBufferD>& b : d->m_DBufs)
            b->update(m_fillBuf);
        for (std::unique_ptr<D3D12TextureD>& t : d->m_DTexs)
            t->update(m_fillBuf);
    }
    for (D3D12Pool* p : gfxF->m_committedPools)
    {
        for (auto& b : p->m_DBufs)
            b.second.m_buf->update(m_fillBuf);
    }
    datalk.unlock();

    /* Perform dynamic uploads */
    if (!m_dynamicNeedsReset)
    {
        m_dynamicCmdList->Close();
        ID3D12CommandList* dcl[] = {m_dynamicCmdList.Get()};
        m_ctx->m_q->ExecuteCommandLists(1, dcl);
        ++m_dynamicBufFenceVal;
        ThrowIfFailed(m_ctx->m_q->Signal(m_dynamicBufFence.Get(), m_dynamicBufFenceVal));
    }

    /* Check on fence */
    if (m_fence->GetCompletedValue() < m_submittedFenceVal)
    {
        /* Abandon this list (renderer too slow) */
        m_cmdList->Close();
        resetCommandList();
        m_dynamicNeedsReset = true;
        m_doPresent = false;
        return;
    }

    /* Perform texture resizes */
    if (m_texResizes.size())
    {
        for (const auto& resize : m_texResizes)
            resize.first->resize(m_ctx, resize.second.first, resize.second.second);
        m_texResizes.clear();
        m_cmdList->Close();
        resetCommandList();
        m_dynamicNeedsReset = true;
        m_doPresent = false;
        return;
    }

    m_drawBuf = m_fillBuf;
    m_fillBuf ^= 1;

    m_cmdList->Close();
    ID3D12CommandList* cl[] = {m_cmdList.Get()};
    m_ctx->m_q->ExecuteCommandLists(1, cl);

    if (m_doPresent)
    {
        ThrowIfFailed(m_windowCtx->m_swapChain->Present(1, 0));
        m_windowCtx->m_backBuf = m_windowCtx->m_swapChain->GetCurrentBackBufferIndex();
        m_doPresent = false;
    }

    ++m_submittedFenceVal;
    ThrowIfFailed(m_ctx->m_q->Signal(m_fence.Get(), m_submittedFenceVal));

    resetCommandList();
    resetDynamicCommandList();
}

IGraphicsCommandQueue* _NewD3D12CommandQueue(D3D12Context* ctx, D3D12Context::Window* windowCtx, IGraphicsContext* parent,
                                             ID3D12CommandQueue** cmdQueueOut)
{
    return new struct D3D12CommandQueue(ctx, windowCtx, parent, cmdQueueOut);
}

IGraphicsDataFactory* _NewD3D12DataFactory(D3D12Context* ctx, IGraphicsContext* parent, uint32_t sampleCount)
{
    return new D3D12DataFactory(parent, ctx, sampleCount);
}

}

#endif // _WIN32_WINNT_WIN10
