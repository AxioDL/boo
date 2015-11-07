#include "../win/Win32Common.hpp"
#if _WIN32_WINNT_WIN10
#include <LogVisor/LogVisor.hpp>
#include "boo/graphicsdev/D3D.hpp"
#include "boo/IGraphicsContext.hpp"
#include <vector>
#include "d3dx12.h"
#include <d3dcompiler.h>
#include <comdef.h>

#define MAX_UNIFORM_COUNT 8
#define MAX_TEXTURE_COUNT 8

extern PFN_D3D12_SERIALIZE_ROOT_SIGNATURE D3D12SerializeRootSignaturePROC;
extern pD3DCompile D3DCompilePROC;

namespace boo
{
static LogVisor::LogModule Log("boo::D3D12");

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

static inline UINT64 NextHeapOffset(UINT64 offset, const D3D12_RESOURCE_ALLOCATION_INFO& info)
{
    offset += info.SizeInBytes;
    return (offset + info.Alignment - 1) & ~(info.Alignment - 1);
}

struct D3D12Data : IGraphicsData
{
    std::vector<std::unique_ptr<class D3D12ShaderPipeline>> m_SPs;
    std::vector<std::unique_ptr<struct D3D12ShaderDataBinding>> m_SBinds;
    std::vector<std::unique_ptr<class D3D12GraphicsBufferS>> m_SBufs;
    std::vector<std::unique_ptr<class D3D12GraphicsBufferD>> m_DBufs;
    std::vector<std::unique_ptr<class D3D12TextureS>> m_STexs;
    std::vector<std::unique_ptr<class D3D12TextureD>> m_DTexs;
    std::vector<std::unique_ptr<class D3D12TextureR>> m_RTexs;
    std::vector<std::unique_ptr<struct D3D12VertexFormat>> m_VFmts;
    ComPtr<ID3D12Heap> m_gpuHeap;
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
    : m_state(USE_TABLE[use]), m_stride(stride), m_count(count), m_sz(stride * count)
    {
        m_gpuDesc = CD3DX12_RESOURCE_DESC::Buffer(m_sz);
        ThrowIfFailed(ctx->m_dev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), 
            D3D12_HEAP_FLAG_NONE, &m_gpuDesc, 
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), &m_buf));

        D3D12_SUBRESOURCE_DATA upData = {data, m_sz, m_sz};
        if (!PrepSubresources<16>(ctx->m_dev.Get(), m_gpuDesc, m_buf.Get(), 0, 0, 1, &upData))
            Log.report(LogVisor::FatalError, "error preparing resource for upload");
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
        CommandSubresourcesTransfer<16>(ctx->m_dev.Get(), ctx->m_loadlist.Get(), m_gpuBuf.Get(), m_buf.Get(), 0, 0, 1);
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
    D3D12GraphicsBufferD(D3D12CommandQueue* q, BufferUse use, D3D12Context* ctx, size_t stride, size_t count)
    : m_state(USE_TABLE[use]), m_q(q), m_stride(stride), m_count(count)
    {
        size_t sz = stride * count;
        for (int i=0 ; i<2 ; ++i)
        {
            ThrowIfFailed(ctx->m_dev->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), 
                D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(sz), 
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), &m_bufs[i]));
        }
    }
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
    size_t m_sz;
    D3D12_RESOURCE_DESC m_gpuDesc;
    D3D12TextureS(D3D12Context* ctx, size_t width, size_t height, size_t mips,
                  TextureFormat fmt, const void* data, size_t sz)
    : m_sz(sz)
    {
        m_gpuDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, mips);
        ThrowIfFailed(ctx->m_dev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), 
            D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(sz), 
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), &m_tex));

        const uint8_t* dataIt = static_cast<const uint8_t*>(data);
        D3D12_SUBRESOURCE_DATA upData[16] = {};
        for (size_t i=0 ; i<m_gpuDesc.MipLevels && i<16 ; ++i)
        {
            upData[i].pData = dataIt;
            upData[i].RowPitch = width * 4;
            upData[i].SlicePitch = upData[i].RowPitch * height;
            dataIt += upData[i].SlicePitch;
            width /= 2;
            height /= 2;
        }

        if (!PrepSubresources<16>(ctx->m_dev.Get(), m_gpuDesc, m_tex.Get(), 0, 0, m_gpuDesc.MipLevels, upData))
            Log.report(LogVisor::FatalError, "error preparing resource for upload");
    }
public:
    ComPtr<ID3D12Resource> m_tex;
    ComPtr<ID3D12Resource> m_gpuTex;
    ~D3D12TextureS() = default;

    UINT64 placeForGPU(D3D12Context* ctx, ID3D12Heap* gpuHeap, UINT64 offset)
    {
        ThrowIfFailed(ctx->m_dev->CreatePlacedResource(gpuHeap, offset, &m_gpuDesc, 
            D3D12_RESOURCE_STATE_COPY_DEST, 
            nullptr, __uuidof(ID3D12Resource), &m_gpuTex));

        CommandSubresourcesTransfer<16>(ctx->m_dev.Get(), ctx->m_loadlist.Get(), m_gpuTex.Get(), m_tex.Get(), 0, 0, m_gpuDesc.MipLevels);
        ctx->m_loadlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_gpuTex.Get(), 
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

        return NextHeapOffset(offset, ctx->m_dev->GetResourceAllocationInfo(0, 1, &m_gpuDesc));
    }
};

class D3D12TextureD : public ITextureD
{
    friend class D3D12DataFactory;
    friend struct D3D12CommandQueue;
    size_t m_width = 0;
    size_t m_height = 0;
    D3D12CommandQueue* m_q;
    D3D12TextureD(D3D12CommandQueue* q, D3D12Context* ctx, size_t width, size_t height, TextureFormat fmt)
    : m_q(q) 
    {
        for (int i=0 ; i<2 ; ++i)
        {
            ThrowIfFailed(ctx->m_dev->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), 
                D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height), 
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), &m_texs[i]));
        }
    }
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
            D3D12_RESOURCE_DESC desc = m_texs[i]->GetDesc();
            ThrowIfFailed(ctx->m_dev->CreatePlacedResource(gpuHeap, offset, &desc, 
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 
                nullptr, __uuidof(ID3D12Resource), &m_gpuTexs[i]));
            offset = NextHeapOffset(offset, ctx->m_dev->GetResourceAllocationInfo(0, 1, &desc));
        }
        return offset;
    }
};

static const float BLACK_COLOR[] = {0.0,0.0,0.0,1.0};

class D3D12TextureR : public ITextureR
{
    friend class D3D12DataFactory;
    friend struct D3D12CommandQueue;
    size_t m_width = 0;
    size_t m_height = 0;
    size_t m_samples = 0;

    void Setup(D3D12Context* ctx, size_t width, size_t height, size_t samples)
    {
        CD3DX12_RESOURCE_DESC rtvresdesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, m_width, m_height, 1, 0, 1, 
            0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        ThrowIfFailed(ctx->m_dev->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, 
            &rtvresdesc, D3D12_RESOURCE_STATE_RENDER_TARGET, &CD3DX12_CLEAR_VALUE(DXGI_FORMAT_R8G8B8A8_UNORM, BLACK_COLOR), 
            __uuidof(ID3D12Resource), &m_gpuTex));

        D3D12_DESCRIPTOR_HEAP_DESC rtvdesc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1};
        ThrowIfFailed(ctx->m_dev->CreateDescriptorHeap(&rtvdesc, __uuidof(ID3D12DescriptorHeap), &m_rtvHeap));

        D3D12_DESCRIPTOR_HEAP_DESC dsvdesc = {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1};
        ThrowIfFailed(ctx->m_dev->CreateDescriptorHeap(&dsvdesc, __uuidof(ID3D12DescriptorHeap), &m_dsvHeap));

        if (samples > 1)
        {
            CD3DX12_RESOURCE_DESC rtvresdesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, m_width, m_height, 1, 0, samples, 
                0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT);
            ThrowIfFailed(ctx->m_dev->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, 
                &rtvresdesc, D3D12_RESOURCE_STATE_RENDER_TARGET, &CD3DX12_CLEAR_VALUE(DXGI_FORMAT_R8G8B8A8_UNORM, BLACK_COLOR), 
                __uuidof(ID3D12Resource), &m_gpuMsaaTex));

            CD3DX12_RESOURCE_DESC dsvresdesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D24_UNORM_S8_UINT, m_width, m_height, 1, 0, samples, 
                0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT);
            ThrowIfFailed(ctx->m_dev->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, 
                &dsvresdesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &CD3DX12_CLEAR_VALUE(DXGI_FORMAT_D24_UNORM_S8_UINT, 1.0, 0), 
                __uuidof(ID3D12Resource), &m_depthTex));

            D3D12_RENDER_TARGET_VIEW_DESC rtvvdesc = {DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RTV_DIMENSION_TEXTURE2D};
            ctx->m_dev->CreateRenderTargetView(m_gpuMsaaTex.Get(), &rtvvdesc, m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

            D3D12_DEPTH_STENCIL_VIEW_DESC dsvvdesc = {DXGI_FORMAT_D24_UNORM_S8_UINT, D3D12_DSV_DIMENSION_TEXTURE2D};
            ctx->m_dev->CreateDepthStencilView(m_depthTex.Get(), &dsvvdesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
        }
        else
        {
            CD3DX12_RESOURCE_DESC dsvresdesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D24_UNORM_S8_UINT, m_width, m_height, 1, 0, 1, 
                0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
            ThrowIfFailed(ctx->m_dev->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, 
                &dsvresdesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &CD3DX12_CLEAR_VALUE(DXGI_FORMAT_D24_UNORM_S8_UINT, 1.0, 0), 
                __uuidof(ID3D12Resource), &m_depthTex));
            
            D3D12_RENDER_TARGET_VIEW_DESC rtvvdesc = {DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RTV_DIMENSION_TEXTURE2D};
            ctx->m_dev->CreateRenderTargetView(m_gpuTex.Get(), &rtvvdesc, m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

            D3D12_DEPTH_STENCIL_VIEW_DESC dsvvdesc = {DXGI_FORMAT_D24_UNORM_S8_UINT, D3D12_DSV_DIMENSION_TEXTURE2D};
            ctx->m_dev->CreateDepthStencilView(m_depthTex.Get(), &dsvvdesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
        }
    }

    D3D12TextureR(D3D12Context* ctx, size_t width, size_t height, size_t samples)
    : m_width(width), m_height(height), m_samples(samples) 
    {
        if (samples == 0) m_samples = 1;
        Setup(ctx, width, height, samples);
    }
public:
    size_t samples() const {return m_samples;}
    ComPtr<ID3D12Resource> m_gpuTex;
    ComPtr<ID3D12Resource> m_gpuMsaaTex;
    ComPtr<ID3D12Resource> m_depthTex;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ~D3D12TextureR() = default;

    void resize(D3D12Context* ctx, size_t width, size_t height)
    {
        if (width < 1)
            width = 1;
        if (height < 1)
            height = 1;
        m_width = width;
        m_height = height;
        Setup(ctx, width, height, m_samples);
    }

    ID3D12Resource* getRenderColorRes() {if (m_samples > 1) return m_gpuMsaaTex.Get(); return m_gpuTex.Get();}
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

struct D3D12VertexFormat : IVertexFormat
{
    size_t m_elementCount;
    std::unique_ptr<D3D12_INPUT_ELEMENT_DESC[]> m_elements;
    D3D12VertexFormat(size_t elementCount, const VertexElementDescriptor* elements)
    : m_elementCount(elementCount),
      m_elements(new D3D12_INPUT_ELEMENT_DESC[elementCount])
    {
        memset(m_elements.get(), 0, elementCount * sizeof(D3D12_INPUT_ELEMENT_DESC));
        size_t offset = 0;
        for (size_t i=0 ; i<elementCount ; ++i)
        {
            const VertexElementDescriptor* elemin = &elements[i];
            D3D12_INPUT_ELEMENT_DESC& elem = m_elements[i];
            elem.SemanticName = SEMANTIC_NAME_TABLE[elemin->semantic];
            elem.SemanticIndex = elemin->semanticIdx;
            elem.Format = SEMANTIC_TYPE_TABLE[elemin->semantic];
            elem.AlignedByteOffset = offset;
            elem.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            offset += SEMANTIC_SIZE_TABLE[elemin->semantic];
        }
    }
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
    D3D12_BLEND_INV_DEST_ALPHA
};

class D3D12ShaderPipeline : public IShaderPipeline
{
    friend class D3D12DataFactory;
    D3D12ShaderPipeline(D3D12Context* ctx, ID3DBlob* vert, ID3DBlob* pixel,
                        const D3D12VertexFormat* vtxFmt,
                        BlendFactor srcFac, BlendFactor dstFac,
                        bool depthTest, bool depthWrite, bool backfaceCulling)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = ctx->m_rs.Get();
        desc.VS = {vert->GetBufferPointer(), vert->GetBufferSize()};
        desc.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        if (dstFac != BlendFactorZero)
        {
            desc.BlendState.RenderTarget[0].BlendEnable = true;
            desc.BlendState.RenderTarget[0].SrcBlend = BLEND_FACTOR_TABLE[srcFac];
            desc.BlendState.RenderTarget[0].DestBlend = BLEND_FACTOR_TABLE[dstFac];
        }
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        if (!backfaceCulling)
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
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
        ThrowIfFailed(ctx->m_dev->CreateGraphicsPipelineState(&desc, __uuidof(ID3D12PipelineState), &m_state));
    }
public:
    ComPtr<ID3D12PipelineState> m_state;
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
    if (tex->type() == ITexture::TextureDynamic)
        return static_cast<D3D12TextureD*>(tex)->placeForGPU(ctx, gpuHeap, offset);
    else if (tex->type() == ITexture::TextureStatic)
        return static_cast<D3D12TextureS*>(tex)->placeForGPU(ctx, gpuHeap, offset);
    return offset;
}

static ID3D12Resource* GetBufferGPUResource(const IGraphicsBuffer* buf, int idx,
                                            D3D12_VERTEX_BUFFER_VIEW& descOut)
{
    if (buf->dynamic())
    {
        const D3D12GraphicsBufferD* cbuf = static_cast<const D3D12GraphicsBufferD*>(buf);
        descOut.SizeInBytes = cbuf->m_count * cbuf->m_stride;
        descOut.StrideInBytes = cbuf->m_stride;
        descOut.BufferLocation = cbuf->m_gpuBufs[idx]->GetGPUVirtualAddress();
        return cbuf->m_gpuBufs[idx].Get();
    }
    else
    {
        const D3D12GraphicsBufferS* cbuf = static_cast<const D3D12GraphicsBufferS*>(buf);
        descOut.SizeInBytes = cbuf->m_count * cbuf->m_stride;
        descOut.StrideInBytes = cbuf->m_stride;
        descOut.BufferLocation = cbuf->m_gpuBuf->GetGPUVirtualAddress();
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

static ID3D12Resource* GetTextureGPUResource(const ITexture* tex, int idx)
{
    if (tex->type() == ITexture::TextureDynamic)
    {
        const D3D12TextureD* ctex = static_cast<const D3D12TextureD*>(tex);
        return ctex->m_gpuTexs[0].Get();
    }
    else if (tex->type() == ITexture::TextureStatic)
    {
        const D3D12TextureS* ctex = static_cast<const D3D12TextureS*>(tex);
        return ctex->m_gpuTex.Get();
    }
    return nullptr;
}

static const struct DefaultTex2DViewDesc : D3D12_SHADER_RESOURCE_VIEW_DESC
{
    DefaultTex2DViewDesc()
    {
        Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        Texture2D = {UINT(0), UINT(-1), UINT(0), 0.0f};
    }
} Tex2DViewDesc;

struct D3D12ShaderDataBinding : IShaderDataBinding
{
    D3D12ShaderPipeline* m_pipeline;
    ComPtr<ID3D12Heap> m_gpuHeap;
    ComPtr<ID3D12DescriptorHeap> m_descHeap[2];
    IGraphicsBuffer* m_vbuf;
    IGraphicsBuffer* m_ibuf;
    size_t m_ubufCount;
    std::unique_ptr<IGraphicsBuffer*[]> m_ubufs;
    size_t m_texCount;
    std::unique_ptr<ITexture*[]> m_texs;
    D3D12_VERTEX_BUFFER_VIEW m_vboView[2];
    D3D12_INDEX_BUFFER_VIEW m_iboView[2];
    D3D12ShaderDataBinding(D3D12Context* ctx,
                           IShaderPipeline* pipeline,
                           IGraphicsBuffer* vbuf, IGraphicsBuffer* ibuf,
                           size_t ubufCount, IGraphicsBuffer** ubufs,
                           size_t texCount, ITexture** texs)
    : m_pipeline(static_cast<D3D12ShaderPipeline*>(pipeline)),
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

            D3D12_CONSTANT_BUFFER_VIEW_DESC viewDesc;

            GetBufferGPUResource(m_vbuf, b, m_vboView[b]);
            if (m_ibuf)
                GetBufferGPUResource(m_ibuf, b, m_iboView[b]);
            for (size_t i=0 ; i<MAX_UNIFORM_COUNT ; ++i)
            {
                if (i<m_ubufCount)
                {
                    GetBufferGPUResource(m_ubufs[i], b, viewDesc);
                    ctx->m_dev->CreateConstantBufferView(&viewDesc, handle);
                }
                handle.Offset(1, incSz);
            }
            for (size_t i=0 ; i<MAX_TEXTURE_COUNT ; ++i)
            {
                if (i<m_texCount)
                {
                    D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc;
                    ctx->m_dev->CreateShaderResourceView(GetTextureGPUResource(m_texs[i], b), &Tex2DViewDesc, handle);
                }
                handle.Offset(1, incSz);
            }
        }
    }

    void bind(ID3D12GraphicsCommandList* list, int b)
    {
        ID3D12DescriptorHeap* heap[] = {m_descHeap[b].Get()};
        list->SetDescriptorHeaps(1, heap);
        list->SetGraphicsRootDescriptorTable(0, m_descHeap[b]->GetGPUDescriptorHandleForHeapStart());
        list->SetPipelineState(m_pipeline->m_state.Get());
        list->IASetVertexBuffers(0, 1, &m_vboView[b]);
        if (m_ibuf)
            list->IASetIndexBuffer(&m_iboView[b]);
    }
};

static ID3D12GraphicsCommandList* WaitForLoadList(D3D12Context* ctx)
{
    /* Wait for previous transaction to complete (if in progress) */
    if (ctx->m_loadfence->GetCompletedValue() < ctx->m_loadfenceval)
    {
        ThrowIfFailed(ctx->m_loadfence->SetEventOnCompletion(ctx->m_loadfenceval, ctx->m_loadfencehandle));
        WaitForSingleObject(ctx->m_loadfencehandle, INFINITE);

        /* Reset allocator and list */
        ThrowIfFailed(ctx->m_loadqalloc->Reset());
        ThrowIfFailed(ctx->m_loadlist->Reset(ctx->m_loadqalloc.Get(), nullptr));
    }
    return ctx->m_loadlist.Get();
}

struct D3D12CommandQueue : IGraphicsCommandQueue
{
    Platform platform() const {return IGraphicsDataFactory::PlatformD3D12;}
    const char* platformName() const {return "Direct 3D 12";}
    D3D12Context* m_ctx;
    D3D12Context::Window* m_windowCtx;
    IGraphicsContext* m_parent;
    ComPtr<ID3D12GraphicsCommandList> m_cmdList;
    ComPtr<ID3D12Fence> m_fence;

    size_t m_fillBuf = 0;
    size_t m_drawBuf = 0;

    void resetCommandList()
    {
        ThrowIfFailed(m_ctx->m_qalloc[m_fillBuf]->Reset());
        ThrowIfFailed(m_cmdList->Reset(m_ctx->m_qalloc[m_fillBuf].Get(), nullptr));
        m_cmdList->SetGraphicsRootSignature(m_ctx->m_rs.Get());
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
        m_cmdList->SetGraphicsRootSignature(m_ctx->m_rs.Get());
    }

    void setShaderDataBinding(IShaderDataBinding* binding)
    {
        D3D12ShaderDataBinding* cbind = static_cast<D3D12ShaderDataBinding*>(binding);
        cbind->bind(m_cmdList.Get(), m_fillBuf);
    }

    D3D12TextureR* m_boundTarget = nullptr;
    void setRenderTarget(ITextureR* target)
    {
        D3D12TextureR* ctarget = static_cast<D3D12TextureR*>(target);

        if (m_boundTarget)
            m_cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_boundTarget->getRenderColorRes(), 
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

        m_cmdList->OMSetRenderTargets(1, &ctarget->m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), 
                                      false, &ctarget->m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

        m_cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(ctarget->getRenderColorRes(), 
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));

        m_boundTarget = ctarget;
    }

    void setViewport(const SWindowRect& rect)
    {
        D3D12_VIEWPORT vp = {rect.location[0], rect.location[1], rect.size[0], rect.size[1], 0.0, 1.0};
        m_cmdList->RSSetViewports(1, &vp);
        D3D12_RECT r = {rect.location[0], rect.location[1], rect.size[0], rect.size[1]};
        m_cmdList->RSSetScissorRects(1, &r);
    }

    std::unordered_map<D3D12TextureR*, std::pair<size_t, size_t>> m_texResizes;
    void resizeRenderTexture(ITextureR* tex, size_t width, size_t height)
    {
        D3D12TextureR* ctex = static_cast<D3D12TextureR*>(tex);
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
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_boundTarget->m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
            m_cmdList->ClearRenderTargetView(handle, m_clearColor, 0, nullptr);
        }
        if (depth)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_boundTarget->m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
            m_cmdList->ClearDepthStencilView(handle, D3D12_CLEAR_FLAG_DEPTH, 1.0, 0, 0, nullptr);
        }
    }

    void setDrawPrimitive(Primitive prim)
    {
        if (prim == PrimitiveTriangles)
            m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        else if (prim == PrimitiveTriStrips)
            m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
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

        if (csource->m_samples > 1)
        {
            ID3D12Resource* src = csource->m_gpuMsaaTex.Get();
            
            D3D12_RESOURCE_BARRIER msaaSetup[] =
            {
                CD3DX12_RESOURCE_BARRIER::Transition(src, 
                    D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(dest.Get(), 
                    D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RESOLVE_DEST)
            };
            m_cmdList->ResourceBarrier(2, msaaSetup);

            m_cmdList->ResolveSubresource(dest.Get(), 0, src, 0, DXGI_FORMAT_R8G8B8A8_UNORM);

            D3D12_RESOURCE_BARRIER msaaTeardown[] =
            {
                CD3DX12_RESOURCE_BARRIER::Transition(src, 
                    D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
                CD3DX12_RESOURCE_BARRIER::Transition(dest.Get(), 
                    D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PRESENT)
            };
            m_cmdList->ResourceBarrier(2, msaaTeardown);
        }
        else
        {
            ID3D12Resource* src = csource->m_gpuTex.Get();

            D3D12_RESOURCE_BARRIER copySetup[] =
            {
                CD3DX12_RESOURCE_BARRIER::Transition(src, 
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(dest.Get(), 
                    D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST)
            };
            m_cmdList->ResourceBarrier(2, copySetup);

            m_cmdList->CopyResource(dest.Get(), src);

            D3D12_RESOURCE_BARRIER copyTeardown[] =
            {
                CD3DX12_RESOURCE_BARRIER::Transition(src, 
                    D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
                CD3DX12_RESOURCE_BARRIER::Transition(dest.Get(), 
                    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT)
            };
            m_cmdList->ResourceBarrier(2, copyTeardown);
        }
        m_doPresent = true;
    }

    UINT64 m_submittedFenceVal = 0;
    void execute()
    {
        /* Check on fence */
        if (m_fence->GetCompletedValue() < m_submittedFenceVal)
        {
            /* Abandon this list (renderer too slow) */
            m_cmdList->Close();
            resetCommandList();
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
            m_doPresent = false;
            return;
        }
        
        m_drawBuf = m_fillBuf;
        ++m_fillBuf;
        if (m_fillBuf == 2)
            m_fillBuf = 0;

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
    }
};

void D3D12GraphicsBufferD::load(const void* data, size_t sz)
{
    ID3D12Resource* res = m_bufs[m_q->m_fillBuf].Get();
    void* d;
    res->Map(0, nullptr, &d);
    memcpy(d, data, sz);
    res->Unmap(0, nullptr);
}
void* D3D12GraphicsBufferD::map(size_t sz)
{
    ID3D12Resource* res = m_bufs[m_q->m_fillBuf].Get();
    void* d;
    res->Map(0, nullptr, &d);
    return d;
}
void D3D12GraphicsBufferD::unmap()
{
    ID3D12Resource* res = m_bufs[m_q->m_fillBuf].Get();
    res->Unmap(0, nullptr);
}

void D3D12TextureD::load(const void* data, size_t sz)
{
    ID3D12Resource* res = m_texs[m_q->m_fillBuf].Get();
    void* d;
    res->Map(0, nullptr, &d);
    memcpy(d, data, sz);
    res->Unmap(0, nullptr);
}
void* D3D12TextureD::map(size_t sz)
{
    ID3D12Resource* res = m_texs[m_q->m_fillBuf].Get();
    void* d;
    res->Map(0, nullptr, &d);
    return d;
}
void D3D12TextureD::unmap()
{
    ID3D12Resource* res = m_texs[m_q->m_fillBuf].Get();
    res->Unmap(0, nullptr);
}

class D3D12DataFactory : public ID3DDataFactory
{
    IGraphicsContext* m_parent;
    IGraphicsData* m_deferredData = nullptr;
    struct D3D12Context* m_ctx;
    std::unordered_set<IGraphicsData*> m_committedData;
public:
    D3D12DataFactory(IGraphicsContext* parent, D3D12Context* ctx)
    : m_parent(parent), m_deferredData(new struct D3D12Data()), m_ctx(ctx)
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
    ~D3D12DataFactory() = default;

    Platform platform() const {return PlatformD3D12;}
    const char* platformName() const {return "Direct3D 12";}

    IGraphicsBufferS* newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count)
    {
        D3D12GraphicsBufferS* retval = new D3D12GraphicsBufferS(use, m_ctx, data, stride, count);
        static_cast<D3D12Data*>(m_deferredData)->m_SBufs.emplace_back(retval);
        return retval;
    }

    IGraphicsBufferS* newStaticBuffer(BufferUse use, std::unique_ptr<uint8_t[]>&& data, size_t stride, size_t count)
    {
        std::unique_ptr<uint8_t[]> d = std::move(data);
        D3D12GraphicsBufferS* retval = new D3D12GraphicsBufferS(use, m_ctx, d.get(), stride, count);
        static_cast<D3D12Data*>(m_deferredData)->m_SBufs.emplace_back(retval);
        return retval;
    }

    IGraphicsBufferD* newDynamicBuffer(BufferUse use, size_t stride, size_t count)
    {
        D3D12CommandQueue* q = static_cast<D3D12CommandQueue*>(m_parent->getCommandQueue());
        D3D12GraphicsBufferD* retval = new D3D12GraphicsBufferD(q, use, m_ctx, stride, count);
        static_cast<D3D12Data*>(m_deferredData)->m_DBufs.emplace_back(retval);
        return retval;
    }

    ITextureS* newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                const void* data, size_t sz)
    {
        D3D12TextureS* retval = new D3D12TextureS(m_ctx, width, height, mips, fmt, data, sz);
        static_cast<D3D12Data*>(m_deferredData)->m_STexs.emplace_back(retval);
        return retval;
    }

    ITextureS* newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                std::unique_ptr<uint8_t[]>&& data, size_t sz)
    {
        std::unique_ptr<uint8_t[]> d = std::move(data);
        D3D12TextureS* retval = new D3D12TextureS(m_ctx, width, height, mips, fmt, d.get(), sz);
        static_cast<D3D12Data*>(m_deferredData)->m_STexs.emplace_back(retval);
        return retval;
    }

    ITextureD* newDynamicTexture(size_t width, size_t height, TextureFormat fmt)
    {
        D3D12CommandQueue* q = static_cast<D3D12CommandQueue*>(m_parent->getCommandQueue());
        D3D12TextureD* retval = new D3D12TextureD(q, m_ctx, width, height, fmt);
        static_cast<D3D12Data*>(m_deferredData)->m_DTexs.emplace_back(retval);
        return retval;
    }

    ITextureR* newRenderTexture(size_t width, size_t height, size_t samples)
    {
        D3D12CommandQueue* q = static_cast<D3D12CommandQueue*>(m_parent->getCommandQueue());
        D3D12TextureR* retval = new D3D12TextureR(m_ctx, width, height, samples);
        static_cast<D3D12Data*>(m_deferredData)->m_RTexs.emplace_back(retval);
        return retval;
    }

    IVertexFormat* newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements)
    {
        D3D12CommandQueue* q = static_cast<D3D12CommandQueue*>(m_parent->getCommandQueue());
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
            IVertexFormat* vtxFmt, BlendFactor srcFac, BlendFactor dstFac,
            bool depthTest, bool depthWrite, bool backfaceCulling)
    {
        ComPtr<ID3DBlob> errBlob;

        if (FAILED(D3DCompilePROC(vertSource, strlen(vertSource), "HECL Vert Source", nullptr, nullptr, "main", 
            "vs_5_0", BOO_D3DCOMPILE_FLAG, 0, &vertBlobOut, &errBlob)))
        {
            Log.report(LogVisor::FatalError, "error compiling vert shader: %s", errBlob->GetBufferPointer());
            return nullptr;
        }

        if (FAILED(D3DCompilePROC(fragSource, strlen(fragSource), "HECL Pixel Source", nullptr, nullptr, "main", 
            "ps_5_0", BOO_D3DCOMPILE_FLAG, 0, &fragBlobOut, &errBlob)))
        {
            Log.report(LogVisor::FatalError, "error compiling pixel shader: %s", errBlob->GetBufferPointer());
            return nullptr;
        }

        D3D12ShaderPipeline* retval = new D3D12ShaderPipeline(m_ctx, vertBlobOut.Get(), fragBlobOut.Get(),
            static_cast<const D3D12VertexFormat*>(vtxFmt),
            srcFac, dstFac, depthTest, depthWrite, backfaceCulling);
        static_cast<D3D12Data*>(m_deferredData)->m_SPs.emplace_back(retval);
        return retval;
    }

    IShaderDataBinding* newShaderDataBinding(IShaderPipeline* pipeline,
            IVertexFormat* vtxFormat,
            IGraphicsBuffer* vbuf, IGraphicsBuffer* ibuf,
            size_t ubufCount, IGraphicsBuffer** ubufs,
            size_t texCount, ITexture** texs)
    {
        D3D12ShaderDataBinding* retval =
            new D3D12ShaderDataBinding(m_ctx, pipeline, vbuf, ibuf, ubufCount, ubufs, texCount, texs);
        static_cast<D3D12Data*>(m_deferredData)->m_SBinds.emplace_back(retval);
        return retval;
    }

    void reset()
    {
        delete static_cast<D3D12Data*>(m_deferredData);
        m_deferredData = new struct D3D12Data();
    }

    IGraphicsData* commit()
    {
        D3D12Data* retval = static_cast<D3D12Data*>(m_deferredData);

        /* Gather resource descriptions */
        std::vector<D3D12_RESOURCE_DESC> descs;
        descs.reserve(retval->m_SBufs.size() + retval->m_DBufs.size() * 2 + 
            retval->m_STexs.size() + retval->m_DTexs.size() * 2);

        for (std::unique_ptr<D3D12GraphicsBufferS>& buf : retval->m_SBufs)
            descs.push_back(buf->m_buf->GetDesc());

        for (std::unique_ptr<D3D12GraphicsBufferD>& buf : retval->m_DBufs)
        {
            descs.push_back(buf->m_bufs[0]->GetDesc());
            descs.push_back(buf->m_bufs[1]->GetDesc());
        }

        for (std::unique_ptr<D3D12TextureS>& tex : retval->m_STexs)
            descs.push_back(tex->m_tex->GetDesc());

        for (std::unique_ptr<D3D12TextureD>& tex : retval->m_DTexs)
        {
            descs.push_back(tex->m_texs[0]->GetDesc());
            descs.push_back(tex->m_texs[1]->GetDesc());
        }

        /* Calculate resources allocation */
        D3D12_RESOURCE_ALLOCATION_INFO allocInfo = 
            m_ctx->m_dev->GetResourceAllocationInfo(0, descs.size(), descs.data());

        /* Create heap */
        ThrowIfFailed(m_ctx->m_dev->CreateHeap(&CD3DX12_HEAP_DESC(allocInfo, 
            D3D12_HEAP_TYPE_DEFAULT), __uuidof(ID3D12Heap), &retval->m_gpuHeap));
        ID3D12Heap* gpuHeap = retval->m_gpuHeap.Get();

        /* Wait for previous transaction to complete */
        WaitForLoadList(m_ctx);

        /* Place resources */
        UINT64 offset = 0;
        for (std::unique_ptr<D3D12GraphicsBufferS>& buf : retval->m_SBufs)
            offset = PlaceBufferForGPU(buf.get(), m_ctx, gpuHeap, offset);

        for (std::unique_ptr<D3D12GraphicsBufferD>& buf : retval->m_DBufs)
            offset = PlaceBufferForGPU(buf.get(), m_ctx, gpuHeap, offset);

        for (std::unique_ptr<D3D12TextureS>& tex : retval->m_STexs)
            offset = PlaceTextureForGPU(tex.get(), m_ctx, gpuHeap, offset);

        for (std::unique_ptr<D3D12TextureD>& tex : retval->m_DTexs)
            offset = PlaceTextureForGPU(tex.get(), m_ctx, gpuHeap, offset);

        /* Execute static uploads */
        ThrowIfFailed(m_ctx->m_loadlist->Close());
        ID3D12CommandList* list[] = {m_ctx->m_loadlist.Get()};
        m_ctx->m_loadq->ExecuteCommandLists(1, list);
        ++m_ctx->m_loadfenceval;
        ThrowIfFailed(m_ctx->m_loadq->Signal(m_ctx->m_loadfence.Get(), m_ctx->m_loadfenceval));

        WaitForLoadList(m_ctx);

        /* Commit data bindings (create descriptor heaps) */
        for (std::unique_ptr<D3D12ShaderDataBinding>& bind : retval->m_SBinds)
            bind->commit(m_ctx);

        /* All set! */
        m_deferredData = new struct D3D12Data();
        m_committedData.insert(retval);
        return retval;
    }

    void destroyData(IGraphicsData* d)
    {
        D3D12Data* data = static_cast<D3D12Data*>(d);
        m_committedData.erase(data);
        delete data;
    }

    void destroyAllData()
    {
        for (IGraphicsData* data : m_committedData)
            delete static_cast<D3D12Data*>(data);
        m_committedData.clear();
    }
};

IGraphicsCommandQueue* _NewD3D12CommandQueue(D3D12Context* ctx, D3D12Context::Window* windowCtx, IGraphicsContext* parent,
                                             ID3D12CommandQueue** cmdQueueOut)
{
    return new struct D3D12CommandQueue(ctx, windowCtx, parent, cmdQueueOut);
}

IGraphicsDataFactory* _NewD3D12DataFactory(D3D12Context* ctx, IGraphicsContext* parent)
{
    return new D3D12DataFactory(parent, ctx);
}

}

#endif // _WIN32_WINNT_WIN10
