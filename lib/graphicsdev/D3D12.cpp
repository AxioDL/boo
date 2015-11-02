#include "boo/graphicsdev/D3D12.hpp"
#include "boo/IGraphicsContext.hpp"
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <LogVisor/LogVisor.hpp>

#include "d3dx12.h"
#include <d3dcompiler.h>

namespace boo
{
static LogVisor::LogModule Log("boo::GL");

static inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        // Set a breakpoint on this line to catch Win32 API errors.
        Log.report(LogVisor::FatalError, "General D3D12 err");
    }
}

static inline UINT64 NextHeapOffset(UINT64 offset, const D3D12_RESOURCE_ALLOCATION_INFO& info)
{
    offset += info.SizeInBytes;
    return (offset + info.Alignment - 1) & ~(info.Alignment - 1);
}

struct D3D12Context
{
    ComPtr<ID3D12Device> m_dev;
    ComPtr<ID3D12CommandAllocator> m_qalloc;
    ComPtr<ID3D12CommandQueue> m_q;
    ComPtr<ID3D12CommandAllocator> m_loadqalloc;
    ComPtr<ID3D12CommandQueue> m_loadq;
    ComPtr<ID3D12Fence> m_frameFence;
    ComPtr<ID3D12RootSignature> m_rs;
    struct Window
    {
        ComPtr<ID3D12Resource> m_fbs[3];
    };
    std::vector<Window> m_windows;
};

struct D3D12Data : IGraphicsData
{
    std::vector<std::unique_ptr<class D3D12ShaderPipeline>> m_SPs;
    std::vector<std::unique_ptr<struct D3D12ShaderDataBinding>> m_SBinds;
    std::vector<std::unique_ptr<class D3D12GraphicsBufferS>> m_SBufs;
    std::vector<std::unique_ptr<class D3D12GraphicsBufferD>> m_DBufs;
    std::vector<std::unique_ptr<class D3D12TextureS>> m_STexs;
    std::vector<std::unique_ptr<class D3D12TextureD>> m_DTexs;
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
    D3D12GraphicsBufferS(BufferUse use, D3D12Context* ctx, const void* data, size_t stride, size_t count)
    : m_state(USE_TABLE[use]), m_stride(stride), m_count(count)
    {
        size_t sz = stride * count;
        ThrowIfFailed(ctx->m_dev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), 
            D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(sz), 
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), &m_buf));
        void* m_d3dBuf;
        m_buf->Map(0, nullptr, &m_d3dBuf);
        memcpy(m_d3dBuf, data, sz);
        m_buf->Unmap(0, nullptr);
    }
public:
    size_t m_stride;
    size_t m_count;
    ComPtr<ID3D12Resource> m_buf;
    ComPtr<ID3D12Resource> m_gpuBuf;
    ~D3D12GraphicsBufferS() = default;

    UINT64 placeForGPU(D3D12Context* ctx, ID3D12Heap* gpuHeap, UINT64 offset)
    {
        D3D12_RESOURCE_DESC desc = m_buf->GetDesc();
        ThrowIfFailed(ctx->m_dev->CreatePlacedResource(gpuHeap, offset, &desc, m_state, 
            nullptr, __uuidof(ID3D12Resource), &m_gpuBuf));
        return NextHeapOffset(offset, ctx->m_dev->GetResourceAllocationInfo(0, 1, &desc));
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
        for (int i=0 ; i<3 ; ++i)
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
    ComPtr<ID3D12Resource> m_bufs[3];
    ComPtr<ID3D12Resource> m_gpuBufs[3];
    ~D3D12GraphicsBufferD() = default;

    void load(const void* data, size_t sz);
    void* map(size_t sz);
    void unmap();

    UINT64 placeForGPU(D3D12Context* ctx, ID3D12Heap* gpuHeap, UINT64 offset)
    {
        for (int i=0 ; i<3 ; ++i)
        {
            D3D12_RESOURCE_DESC desc = m_bufs[i]->GetDesc();
            ThrowIfFailed(ctx->m_dev->CreatePlacedResource(gpuHeap, offset, &desc, m_state, 
                nullptr, __uuidof(ID3D12Resource), &m_gpuBufs[i]));
            offset = NextHeapOffset(offset, ctx->m_dev->GetResourceAllocationInfo(0, 1, &desc));
        }
        return offset;
    }
};

const IGraphicsBufferS*
D3D12DataFactory::newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count)
{
    D3D12GraphicsBufferS* retval = new D3D12GraphicsBufferS(use, m_ctx, data, stride, count);
    static_cast<D3D12Data*>(m_deferredData)->m_SBufs.emplace_back(retval);
    return retval;
}

class D3D12TextureS : public ITextureS
{
    friend class D3D12DataFactory;
    D3D12TextureS(D3D12Context* ctx, size_t width, size_t height, size_t mips,
                  TextureFormat fmt, const void* data, size_t sz)
    {
        ThrowIfFailed(ctx->m_dev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), 
            D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, mips), 
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), &m_tex));
        const uint8_t* dataIt = static_cast<const uint8_t*>(data);
        if (fmt == TextureFormatRGBA8)
        {
            for (size_t i=0 ; i<mips ; ++i)
            {
                void* data;
                m_tex->Map(i, nullptr, &data);
                size_t thisSz = width * height * 4;
                memcpy(data, dataIt, thisSz);
                m_tex->Unmap(i, nullptr);
                dataIt += thisSz;
                width /= 2;
                height /= 2;
            }
        }
    }
public:
    ComPtr<ID3D12Resource> m_tex;
    ComPtr<ID3D12Resource> m_gpuTex;
    ~D3D12TextureS() = default;

    UINT64 placeForGPU(D3D12Context* ctx, ID3D12Heap* gpuHeap, UINT64 offset)
    {
        D3D12_RESOURCE_DESC desc = m_tex->GetDesc();
        ThrowIfFailed(ctx->m_dev->CreatePlacedResource(gpuHeap, offset, &desc, 
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 
            nullptr, __uuidof(ID3D12Resource), &m_gpuTex));
        return NextHeapOffset(offset, ctx->m_dev->GetResourceAllocationInfo(0, 1, &desc));
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
        ThrowIfFailed(ctx->m_dev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), 
            D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height), 
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), &m_texs[0]));
        ThrowIfFailed(ctx->m_dev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), 
            D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D24_UNORM_S8_UINT, width, height), 
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), &m_texs[1]));
    }
public:
    ComPtr<ID3D12Resource> m_texs[2];
    ComPtr<ID3D12Resource> m_gpuTexs[2];
    ~D3D12TextureD() = default;

    void load(const void* data, size_t sz)
    {
        void* buf;
        m_texs[0]->Map(0, nullptr, &buf);
        memcpy(buf, data, sz);
        m_texs[0]->Unmap(0, nullptr);
    }
    void* map(size_t sz)
    {
        void* buf;
        m_texs[0]->Map(0, nullptr, &buf);
        return buf;
    }
    void unmap()
    {
        m_texs[0]->Unmap(0, nullptr);
    }

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

const ITextureS*
D3D12DataFactory::newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                   const void* data, size_t sz)
{
    D3D12TextureS* retval = new D3D12TextureS(m_ctx, width, height, mips, fmt, data, sz);
    static_cast<D3D12Data*>(m_deferredData)->m_STexs.emplace_back(retval);
    return retval;
}

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
        for (size_t i=0 ; i<elementCount ; ++i)
        {
            D3D12_INPUT_ELEMENT_DESC& elem = m_elements[i];
            elem.SemanticName = SEMANTIC_NAME_TABLE[elements->semantic];
            elem.SemanticIndex = elements->semanticIdx;
            elem.Format = SEMANTIC_TYPE_TABLE[elements->semantic];
            elem.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
            elem.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
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
        desc.SampleDesc.Count = 1;
        ThrowIfFailed(ctx->m_dev->CreateGraphicsPipelineState(&desc, __uuidof(ID3D12PipelineState), &m_state));
    }
public:
    ComPtr<ID3D12PipelineState> m_state;
    ~D3D12ShaderPipeline() = default;
    D3D12ShaderPipeline& operator=(const D3D12ShaderPipeline&) = delete;
    D3D12ShaderPipeline(const D3D12ShaderPipeline&) = delete;
};

const IShaderPipeline* D3D12DataFactory::newShaderPipeline
(const char* vertSource, const char* fragSource,
 ComPtr<ID3DBlob>& vertBlobOut, ComPtr<ID3DBlob>& fragBlobOut,
 const IVertexFormat* vtxFmt,
 BlendFactor srcFac, BlendFactor dstFac,
 bool depthTest, bool depthWrite, bool backfaceCulling)
{
    ComPtr<ID3DBlob> errBlob;

    if (FAILED(D3DCompile(vertSource, strlen(vertSource), "HECL Vert Source", nullptr, nullptr, "main", 
        "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vertBlobOut, &errBlob)))
    {
        Log.report(LogVisor::FatalError, "error compiling vert shader: %s", errBlob->GetBufferPointer());
        return nullptr;
    }

    if (FAILED(D3DCompile(fragSource, strlen(fragSource), "HECL Pixel Source", nullptr, nullptr, "main", 
        "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &fragBlobOut, &errBlob)))
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

static UINT64 PlaceBufferForGPU(IGraphicsBuffer* buf, D3D12Context* ctx, ID3D12Heap* gpuHeap, UINT64 offset)
{
    if (buf->dynamic())
        return static_cast<D3D12GraphicsBufferD*>(buf)->placeForGPU(ctx, gpuHeap, offset);
    else
        return static_cast<D3D12GraphicsBufferS*>(buf)->placeForGPU(ctx, gpuHeap, offset);
}

static UINT64 PlaceTextureForGPU(ITexture* tex, D3D12Context* ctx, ID3D12Heap* gpuHeap, UINT64 offset)
{
    if (tex->dynamic())
        return static_cast<D3D12TextureD*>(tex)->placeForGPU(ctx, gpuHeap, offset);
    else
        return static_cast<D3D12TextureS*>(tex)->placeForGPU(ctx, gpuHeap, offset);
}

static ID3D12Resource* GetBufferGPUResource(const IGraphicsBuffer* buf, int idx,
                                            D3D12_SHADER_RESOURCE_VIEW_DESC& descOut)
{
    descOut.Format = DXGI_FORMAT_UNKNOWN;
    descOut.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    descOut.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    descOut.Buffer.FirstElement = 0;
    descOut.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    if (buf->dynamic())
    {
        const D3D12GraphicsBufferD* cbuf = static_cast<const D3D12GraphicsBufferD*>(buf);
        descOut.Buffer.NumElements = cbuf->m_count;
        descOut.Buffer.StructureByteStride = cbuf->m_stride;
        return cbuf->m_gpuBufs[idx].Get();
    }
    else
    {
        const D3D12GraphicsBufferS* cbuf = static_cast<const D3D12GraphicsBufferS*>(buf);
        descOut.Buffer.NumElements = cbuf->m_count;
        descOut.Buffer.StructureByteStride = cbuf->m_stride;
        return cbuf->m_gpuBuf.Get();
    }
}

static ID3D12Resource* GetTextureGPUResource(const ITexture* tex, int idx)
{
    if (tex->dynamic())
    {
        const D3D12TextureD* ctex = static_cast<const D3D12TextureD*>(tex);
        return ctex->m_gpuTexs[0].Get();
    }
    else
    {
        const D3D12TextureS* ctex = static_cast<const D3D12TextureS*>(tex);
        return ctex->m_gpuTex.Get();
    }
}

static const struct DefaultTex2DViewDesc : D3D12_SHADER_RESOURCE_VIEW_DESC
{
    DefaultTex2DViewDesc()
    {
        Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        Texture2D = {0, -1, 0, 0.0f};
    }
} Tex2DViewDesc;

struct D3D12ShaderDataBinding : IShaderDataBinding
{
    D3D12ShaderPipeline* m_pipeline;
    ComPtr<ID3D12Heap> m_gpuHeap;
    ComPtr<ID3D12DescriptorHeap> m_descHeap[3];
    IGraphicsBuffer* m_vbuf;
    IGraphicsBuffer* m_ibuf;
    size_t m_ubufCount;
    std::unique_ptr<IGraphicsBuffer*[]> m_ubufs;
    size_t m_texCount;
    std::unique_ptr<ITexture*[]> m_texs;
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
        /* Create triple-buffered descriptor heaps */
        D3D12_DESCRIPTOR_HEAP_DESC desc;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 2 + m_ubufCount + m_texCount;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        desc.NodeMask = 0;

        UINT incSz = ctx->m_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (int b=0 ; b<3 ; ++b)
        {
            ThrowIfFailed(ctx->m_dev->CreateDescriptorHeap(&desc, _uuidof(ID3D12DescriptorHeap), &m_descHeap[b]));
            CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_descHeap[b]->GetCPUDescriptorHandleForHeapStart());
            D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc;

            ctx->m_dev->CreateShaderResourceView(GetBufferGPUResource(m_vbuf, b, viewDesc), &viewDesc, handle);
            handle.Offset(1, incSz);
            if (m_ibuf)
                ctx->m_dev->CreateShaderResourceView(GetBufferGPUResource(m_ibuf, b, viewDesc), &viewDesc, handle);
            handle.Offset(1, incSz);
            for (size_t i=0 ; i<m_ubufCount ; ++i)
            {
                ctx->m_dev->CreateShaderResourceView(GetBufferGPUResource(m_ubufs[i], b, viewDesc), &viewDesc, handle);
                handle.Offset(1, incSz);
            }
            for (size_t i=0 ; i<m_texCount ; ++i)
            {
                ctx->m_dev->CreateShaderResourceView(GetTextureGPUResource(m_texs[i], b), &Tex2DViewDesc, handle);
                handle.Offset(1, incSz);
            }
        }
    }
};

const IShaderDataBinding*
D3D12DataFactory::newShaderDataBinding(IShaderPipeline* pipeline,
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

D3D12DataFactory::D3D12DataFactory(IGraphicsContext* parent, D3D12Context* ctx)
: m_parent(parent), m_deferredData(new struct D3D12Data()), m_ctx(ctx)
{
    CD3DX12_DESCRIPTOR_RANGE cbvRange(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 2, 0);
    CD3DX12_DESCRIPTOR_RANGE srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0);
    CD3DX12_ROOT_PARAMETER rootParms[2];
    rootParms[0].InitAsDescriptorTable(1, &cbvRange, D3D12_SHADER_VISIBILITY_VERTEX);
    rootParms[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    ComPtr<ID3DBlob> rsOutBlob;
    ComPtr<ID3DBlob> rsErrorBlob;
    ThrowIfFailed(D3D12SerializeRootSignature(
        &CD3DX12_ROOT_SIGNATURE_DESC(2, rootParms, 1, &CD3DX12_STATIC_SAMPLER_DESC(0)), 
        D3D_ROOT_SIGNATURE_VERSION_1, &rsOutBlob, &rsErrorBlob));

    ThrowIfFailed(ctx->m_dev->CreateRootSignature(0, rsOutBlob->GetBufferPointer(), 
        rsOutBlob->GetBufferSize(), __uuidof(ID3D12RootSignature), &ctx->m_rs));
}

void D3D12DataFactory::reset()
{
    delete static_cast<D3D12Data*>(m_deferredData);
    m_deferredData = new struct D3D12Data();
}

IGraphicsData* D3D12DataFactory::commit()
{
    D3D12Data* retval = static_cast<D3D12Data*>(m_deferredData);

    /* Gather resource descriptions */
    std::vector<D3D12_RESOURCE_DESC> descs;
    descs.reserve(retval->m_SBufs.size() + retval->m_DBufs.size() * 3 + 
                  retval->m_STexs.size() + retval->m_DTexs.size() * 2);

    for (std::unique_ptr<D3D12GraphicsBufferS>& buf : retval->m_SBufs)
        descs.push_back(buf->m_buf->GetDesc());

    for (std::unique_ptr<D3D12GraphicsBufferD>& buf : retval->m_DBufs)
    {
        descs.push_back(buf->m_bufs[0]->GetDesc());
        descs.push_back(buf->m_bufs[1]->GetDesc());
        descs.push_back(buf->m_bufs[2]->GetDesc());
    }

    for (std::unique_ptr<D3D12TextureS>& tex : retval->m_STexs)
        descs.push_back(tex->m_tex->GetDesc());

    for (std::unique_ptr<D3D12TextureD>& tex : retval->m_DTexs)
    {
        descs.push_back(tex->m_texs[0]->GetDesc());
        descs.push_back(tex->m_texs[1]->GetDesc());
        descs.push_back(tex->m_texs[2]->GetDesc());
    }

    /* Calculate resources allocation */
    D3D12_RESOURCE_ALLOCATION_INFO allocInfo = 
        m_ctx->m_dev->GetResourceAllocationInfo(0, descs.size(), descs.data());

    /* Create heap */
    ThrowIfFailed(m_ctx->m_dev->CreateHeap(&CD3DX12_HEAP_DESC(allocInfo, 
        D3D12_HEAP_TYPE_DEFAULT), __uuidof(ID3D12Heap), &retval->m_gpuHeap));
    ID3D12Heap* gpuHeap = retval->m_gpuHeap.Get();

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

    /* Commit data bindings (create descriptor heaps) */
    for (std::unique_ptr<D3D12ShaderDataBinding>& bind : retval->m_SBinds)
        bind->commit(m_ctx);

    /* All set! */
    m_deferredData = new struct D3D12Data();
    m_committedData.insert(retval);
    return retval;
}

void D3D12DataFactory::destroyData(IGraphicsData* d)
{
    D3D12Data* data = static_cast<D3D12Data*>(d);
    m_committedData.erase(data);
    delete data;
}

void D3D12DataFactory::destroyAllData()
{
    for (IGraphicsData* data : m_committedData)
        delete static_cast<D3D12Data*>(data);
    m_committedData.clear();
}

struct D3D12CommandQueue : IGraphicsCommandQueue
{
    Platform platform() const {return IGraphicsDataFactory::PlatformD3D12;}
    const char* platformName() const {return "Direct 3D 12";}
    D3D12Context* m_ctx;
    IGraphicsContext* m_parent;
    ComPtr<ID3D12GraphicsCommandList> m_cmdList;

    size_t m_fillBuf = 0;
    size_t m_completeBuf = 0;
    size_t m_drawBuf = 0;

    D3D12CommandQueue(D3D12Context* ctx, IGraphicsContext* parent)
    : m_ctx(ctx), m_parent(parent)
    {
        ThrowIfFailed(ctx->m_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, 
                                                         __uuidof(ID3D12CommandAllocator),
                                                         &ctx->m_qalloc));
        D3D12_COMMAND_QUEUE_DESC desc = 
        {
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            D3D12_COMMAND_QUEUE_PRIORITY_HIGH,
            D3D12_COMMAND_QUEUE_FLAG_NONE
        };
        ThrowIfFailed(ctx->m_dev->CreateCommandQueue(&desc, __uuidof(ID3D12CommandQueue), &ctx->m_q));
        ThrowIfFailed(ctx->m_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, 
                                              __uuidof(ID3D12Fence), &ctx->m_frameFence));
        ThrowIfFailed(ctx->m_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ctx->m_qalloc.Get(), 
                                                    nullptr, __uuidof(ID3D12GraphicsCommandList), &m_cmdList));
        m_cmdList->SetGraphicsRootSignature(m_ctx->m_rs.Get());
    }

    void setShaderDataBinding(const IShaderDataBinding* binding)
    {
        const D3D12ShaderDataBinding* cbind = static_cast<const D3D12ShaderDataBinding*>(binding);
        ID3D12DescriptorHeap* descHeap = cbind->m_descHeap[m_fillBuf].Get();
        m_cmdList->SetDescriptorHeaps(1, &descHeap);
        m_cmdList->SetPipelineState(cbind->m_pipeline->m_state.Get());
    }
    void setRenderTarget(const ITextureD* target)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::OpSetRenderTarget);
        cmds.back().target = target;
    }

    void setClearColor(const float rgba[4])
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::OpSetClearColor);
        cmds.back().rgba[0] = rgba[0];
        cmds.back().rgba[1] = rgba[1];
        cmds.back().rgba[2] = rgba[2];
        cmds.back().rgba[3] = rgba[3];
    }
    void clearTarget(bool render=true, bool depth=true)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::OpClearTarget);
        cmds.back().flags = 0;
        if (render)
            cmds.back().flags |= GL_COLOR_BUFFER_BIT;
        if (depth)
            cmds.back().flags |= GL_DEPTH_BUFFER_BIT;
    }

    void setDrawPrimitive(Primitive prim)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::OpSetDrawPrimitive);
        if (prim == PrimitiveTriangles)
            cmds.back().prim = GL_TRIANGLES;
        else if (prim == PrimitiveTriStrips)
            cmds.back().prim = GL_TRIANGLE_STRIP;
    }
    void draw(size_t start, size_t count)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::OpDraw);
        cmds.back().start = start;
        cmds.back().count = count;
    }
    void drawIndexed(size_t start, size_t count)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::OpDrawIndexed);
        cmds.back().start = start;
        cmds.back().count = count;
    }
    void drawInstances(size_t start, size_t count, size_t instCount)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::OpDrawInstances);
        cmds.back().start = start;
        cmds.back().count = count;
        cmds.back().instCount = instCount;
    }
    void drawInstancesIndexed(size_t start, size_t count, size_t instCount)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::OpDrawInstancesIndexed);
        cmds.back().start = start;
        cmds.back().count = count;
        cmds.back().instCount = instCount;
    }

    void present()
    {
        m_cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
    }

    void execute()
    {
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
        m_cmdBufs[m_fillBuf].clear();
    }
};

void D3D12GraphicsBufferD::load(const void* data, size_t sz)
{
    glBindBuffer(m_target, m_bufs[m_q->m_fillBuf]);
    glBufferData(m_target, sz, data, GL_DYNAMIC_DRAW);
}
void* D3D12GraphicsBufferD::map(size_t sz)
{
    if (m_mappedBuf)
        free(m_mappedBuf);
    m_mappedBuf = malloc(sz);
    m_mappedSize = sz;
    return m_mappedBuf;
}
void D3D12GraphicsBufferD::unmap()
{
    glBindBuffer(m_target, m_bufs[m_q->m_fillBuf]);
    glBufferData(m_target, m_mappedSize, m_mappedBuf, GL_DYNAMIC_DRAW);
    free(m_mappedBuf);
    m_mappedBuf = nullptr;
}

IGraphicsBufferD*
D3D12DataFactory::newDynamicBuffer(BufferUse use, size_t stride, size_t count)
{
    D3D12GraphicsBufferD* retval = new D3D12GraphicsBufferD(use, stride, count);
    static_cast<D3D12Data*>(m_deferredData)->m_DBufs.emplace_back(retval);
    return retval;
}

ITextureD*
D3D12DataFactory::newDynamicTexture(size_t width, size_t height, TextureFormat fmt)
{
    D3D12CommandQueue* q = static_cast<D3D12CommandQueue*>(m_parent->getCommandQueue());
    D3D12TextureD* retval = new D3D12TextureD(q, width, height, fmt);
    static_cast<D3D12Data*>(m_deferredData)->m_DTexs.emplace_back(retval);
    return retval;
}

const IVertexFormat* D3D12DataFactory::newVertexFormat
(size_t elementCount, const VertexElementDescriptor* elements)
{
    D3D12CommandQueue* q = static_cast<D3D12CommandQueue*>(m_parent->getCommandQueue());
    D3D12VertexFormat* retval = new struct D3D12VertexFormat(q, elementCount, elements);
    static_cast<D3D12Data*>(m_deferredData)->m_VFmts.emplace_back(retval);
    return retval;
}

IGraphicsCommandQueue* _NewD3D12CommandQueue(IGraphicsContext* parent)
{
    return new struct D3D12CommandQueue(parent);
}

}
