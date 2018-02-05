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
#include "xxhash.h"

#define MAX_UNIFORM_COUNT 8
#define MAX_TEXTURE_COUNT 8

#undef min
#undef max

extern PFN_D3D12_SERIALIZE_ROOT_SIGNATURE D3D12SerializeRootSignaturePROC;
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
static logvisor::Module Log("boo::D3D12");
class D3D12DataFactory;

struct D3D12ShareableShader : IShareableShader<D3D12DataFactory, D3D12ShareableShader>
{
    ComPtr<ID3DBlob> m_shader;
    D3D12ShareableShader(D3D12DataFactory& fac, uint64_t srcKey, uint64_t binKey, ComPtr<ID3DBlob>&& s)
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
        _com_error err(hr, L"D3D12 fail");
#endif
        LPCTSTR errMsg = err.ErrorMessage();
        Log.report(logvisor::Fatal, errMsg);
    }
}

static inline UINT64 NextHeapOffset(UINT64 offset, const D3D12_RESOURCE_ALLOCATION_INFO& info)
{
    offset += info.SizeInBytes;
    return (offset + info.Alignment - 1) & ~(info.Alignment - 1);
}

struct D3D12Data : BaseGraphicsData
{
    ComPtr<ID3D12Heap> m_bufHeap;
    ComPtr<ID3D12Heap> m_texHeap;
    explicit D3D12Data(GraphicsDataFactoryHead& head) : BaseGraphicsData(head) {}
};

struct D3D12Pool : BaseGraphicsPool
{
    ComPtr<ID3D12Heap> m_bufHeap;
    explicit D3D12Pool(GraphicsDataFactoryHead& head) : BaseGraphicsPool(head) {}
};

static const D3D12_RESOURCE_STATES USE_TABLE[] =
{
    D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
    D3D12_RESOURCE_STATE_INDEX_BUFFER,
    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
};

class D3D12GraphicsBufferS : public GraphicsDataNode<IGraphicsBufferS>
{
    friend class D3D12DataFactory;
    friend struct D3D12CommandQueue;
    D3D12_RESOURCE_STATES m_state;
    size_t m_sz;
    D3D12_RESOURCE_DESC m_gpuDesc;
    D3D12GraphicsBufferS(const boo::ObjToken<BaseGraphicsData>& parent,
                         BufferUse use, D3D12Context* ctx,
                         const void* data, size_t stride, size_t count)
    : GraphicsDataNode<IGraphicsBufferS>(parent), m_state(USE_TABLE[int(use)]),
      m_stride(stride), m_count(count), m_sz(stride * count)
    {
        size_t gpuSz = use == BufferUse::Uniform ? ((m_sz + 255) & ~255) : m_sz;
        m_gpuDesc = CD3DX12_RESOURCE_DESC::Buffer(gpuSz);
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
        ctx->m_loadlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_gpuBuf.Get(),
            m_state, D3D12_RESOURCE_STATE_COPY_DEST));
        CommandSubresourcesTransfer<1>(ctx->m_dev.Get(), ctx->m_loadlist.Get(), m_gpuBuf.Get(), m_buf.Get(), 0, 0, 1);
        ctx->m_loadlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_gpuBuf.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, m_state));

        return NextHeapOffset(offset, ctx->m_dev->GetResourceAllocationInfo(0, 1, &m_gpuDesc));
    }
};

template <class DataCls>
class D3D12GraphicsBufferD : public GraphicsDataNode<IGraphicsBufferD, DataCls>
{
    friend class D3D12DataFactory;
    friend struct D3D12CommandQueue;
    D3D12CommandQueue* m_q;
    D3D12_RESOURCE_STATES m_state;
    std::unique_ptr<uint8_t[]> m_cpuBuf;
    size_t m_cpuSz;
    int m_validSlots = 0;
    D3D12GraphicsBufferD(const boo::ObjToken<DataCls>& parent,
                         D3D12CommandQueue* q, BufferUse use,
                         D3D12Context* ctx, size_t stride, size_t count)
    : GraphicsDataNode<IGraphicsBufferD, DataCls>(parent),
      m_state(USE_TABLE[int(use)]), m_q(q), m_stride(stride), m_count(count)
    {
        m_cpuSz = stride * count;
        size_t gpuSz = ((m_cpuSz + 255) & ~255);
        m_cpuBuf.reset(new uint8_t[gpuSz]);
        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(gpuSz);
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

class D3D12TextureS : public GraphicsDataNode<ITextureS>
{
    friend class D3D12DataFactory;
    TextureFormat m_fmt;
    size_t m_sz;
    D3D12_RESOURCE_DESC m_gpuDesc;
    D3D12TextureS(const boo::ObjToken<BaseGraphicsData>& parent,
                  D3D12Context* ctx, size_t width, size_t height, size_t mips,
                  TextureFormat fmt, const void* data, size_t sz)
    : GraphicsDataNode<ITextureS>(parent), m_fmt(fmt), m_sz(sz), m_mipCount(mips)
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
            if (compressed)
                upData[i].RowPitch = width * 2;
            dataIt += upData[i].SlicePitch;
            if (width > 1)
                width /= 2;
            if (height > 1)
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

class D3D12TextureSA : public GraphicsDataNode<ITextureSA>
{
    friend class D3D12DataFactory;
    TextureFormat m_fmt;
    size_t m_layers;
    size_t m_sz;
    D3D12_RESOURCE_DESC m_gpuDesc;
    D3D12TextureSA(const boo::ObjToken<BaseGraphicsData>& parent, D3D12Context* ctx,
                   size_t width, size_t height, size_t layers,
                   size_t mips, TextureFormat fmt, const void* data, size_t sz)
    : GraphicsDataNode<ITextureSA>(parent), m_fmt(fmt), m_layers(layers), m_sz(sz)
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
        case TextureFormat::I16:
            pxPitch = 2;
            pixelFmt = DXGI_FORMAT_R16_UNORM;
            break;
        default:
            Log.report(logvisor::Fatal, "unsupported tex format");
        }

        m_gpuDesc = CD3DX12_RESOURCE_DESC::Tex2D(pixelFmt, width, height, layers, mips);
        size_t reqSz = GetRequiredIntermediateSize(ctx->m_dev.Get(), &m_gpuDesc, 0, layers * mips);
        ThrowIfFailed(ctx->m_dev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(reqSz),
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), &m_tex));

        const uint8_t* dataIt = static_cast<const uint8_t*>(data);
        std::unique_ptr<D3D12_SUBRESOURCE_DATA[]> upData(new D3D12_SUBRESOURCE_DATA[layers * mips]);
        D3D12_SUBRESOURCE_DATA* outIt = upData.get();
        for (size_t i=0 ; i<mips ; ++i)
        {
            for (size_t j=0 ; j<layers ; ++j)
            {
                outIt->pData = dataIt;
                outIt->RowPitch = width * pxPitch;
                outIt->SlicePitch = outIt->RowPitch * height;
                dataIt += outIt->SlicePitch;
                ++outIt;
            }
            if (width > 1)
                width /= 2;
            if (height > 1)
                height /= 2;
        }
        if (!PrepSubresources(ctx->m_dev.Get(), &m_gpuDesc, m_tex.Get(), 0, 0, layers * mips, upData.get()))
            Log.report(logvisor::Fatal, "error preparing resource for upload");
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

        CommandSubresourcesTransfer(ctx->m_dev.Get(), ctx->m_loadlist.Get(), m_gpuTex.Get(),
                                    m_tex.Get(), 0, 0, m_gpuDesc.DepthOrArraySize * m_gpuDesc.MipLevels);
        ctx->m_loadlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_gpuTex.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

        return NextHeapOffset(offset, ctx->m_dev->GetResourceAllocationInfo(0, 1, &m_gpuDesc));
    }

    TextureFormat format() const {return m_fmt;}
    size_t layers() const {return m_layers;}
};

class D3D12TextureD : public GraphicsDataNode<ITextureD>
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
    D3D12TextureD(const boo::ObjToken<BaseGraphicsData>& parent, D3D12CommandQueue* q, D3D12Context* ctx,
                  size_t width, size_t height, TextureFormat fmt)
    : GraphicsDataNode<ITextureD>(parent), m_width(width), m_height(height), m_fmt(fmt), m_q(q)
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
        case TextureFormat::I16:
            pixelFmt = DXGI_FORMAT_R16_UNORM;
            pxPitch = 2;
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
#define MAX_BIND_TEXS 4

class D3D12TextureR : public GraphicsDataNode<ITextureR>
{
    friend class D3D12DataFactory;
    friend struct D3D12CommandQueue;
    size_t m_width = 0;
    size_t m_height = 0;
    size_t m_samples = 0;
    size_t m_colorBindCount;
    size_t m_depthBindCount;

    void Setup(D3D12Context* ctx)
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

        if (m_samples > 1)
        {
            rtvDim = D3D12_RTV_DIMENSION_TEXTURE2DMS;
            dsvDim = D3D12_DSV_DIMENSION_TEXTURE2DMS;
            rtvresdesc = CD3DX12_RESOURCE_DESC::Tex2D(ctx->RGBATex2DFBViewDesc.Format, m_width, m_height, 1, 1, m_samples,
                0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_TEXTURE_LAYOUT_UNKNOWN,
                D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT);
            dsvresdesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R24G8_TYPELESS, m_width, m_height, 1, 1, m_samples,
                0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_TEXTURE_LAYOUT_UNKNOWN,
                D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT);
        }
        else
        {
            rtvDim = D3D12_RTV_DIMENSION_TEXTURE2D;
            dsvDim = D3D12_DSV_DIMENSION_TEXTURE2D;
            rtvresdesc = CD3DX12_RESOURCE_DESC::Tex2D(ctx->RGBATex2DFBViewDesc.Format, m_width, m_height, 1, 1, 1,
                0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
            dsvresdesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R24G8_TYPELESS, m_width, m_height, 1, 1, 1,
                0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        }

        cbindresdesc = CD3DX12_RESOURCE_DESC::Tex2D(ctx->RGBATex2DFBViewDesc.Format, m_width, m_height, 1, 1, 1,
            0, D3D12_RESOURCE_FLAG_NONE);
        dbindresdesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R24G8_TYPELESS, m_width, m_height, 1, 1, 1,
            0, D3D12_RESOURCE_FLAG_NONE);

        D3D12_CLEAR_VALUE colorClear = {};
        colorClear.Format = ctx->RGBATex2DFBViewDesc.Format;
        ThrowIfFailed(ctx->m_dev->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
            &rtvresdesc, D3D12_RESOURCE_STATE_RENDER_TARGET, &colorClear,
            __uuidof(ID3D12Resource), &m_colorTex));

        D3D12_CLEAR_VALUE depthClear = {};
        depthClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        ThrowIfFailed(ctx->m_dev->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
            &dsvresdesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
            __uuidof(ID3D12Resource), &m_depthTex));

        D3D12_RENDER_TARGET_VIEW_DESC rtvvdesc = {ctx->RGBATex2DFBViewDesc.Format, rtvDim};
        ctx->m_dev->CreateRenderTargetView(m_colorTex.Get(), &rtvvdesc, m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvvdesc = {DXGI_FORMAT_D24_UNORM_S8_UINT, dsvDim};
        ctx->m_dev->CreateDepthStencilView(m_depthTex.Get(), &dsvvdesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

        for (size_t i=0 ; i<m_colorBindCount ; ++i)
        {
            ThrowIfFailed(ctx->m_dev->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
                &cbindresdesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                __uuidof(ID3D12Resource), &m_colorBindTex[i]));
        }

        for (size_t i=0 ; i<m_depthBindCount ; ++i)
        {
            ThrowIfFailed(ctx->m_dev->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
                &dbindresdesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                __uuidof(ID3D12Resource), &m_depthBindTex[i]));
        }
    }

    D3D12CommandQueue* m_q;
    D3D12TextureR(const boo::ObjToken<BaseGraphicsData>& parent,
                  D3D12Context* ctx, D3D12CommandQueue* q,
                  size_t width, size_t height, size_t samples,
                  size_t colorBindCount, size_t depthBindCount)
    : GraphicsDataNode<ITextureR>(parent), m_q(q), m_width(width), m_height(height), m_samples(samples),
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
    ComPtr<ID3D12Resource> m_colorTex;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;

    ComPtr<ID3D12Resource> m_depthTex;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;

    ComPtr<ID3D12Resource> m_colorBindTex[MAX_BIND_TEXS];

    ComPtr<ID3D12Resource> m_depthBindTex[MAX_BIND_TEXS];

    ~D3D12TextureR();

    void resize(D3D12Context* ctx, size_t width, size_t height)
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

struct D3D12VertexFormat : GraphicsDataNode<IVertexFormat>
{
    size_t m_elementCount;
    std::unique_ptr<D3D12_INPUT_ELEMENT_DESC[]> m_elements;
    size_t m_stride = 0;
    size_t m_instStride = 0;

    D3D12VertexFormat(const boo::ObjToken<BaseGraphicsData>& parent,
                      size_t elementCount, const VertexElementDescriptor* elements)
    : GraphicsDataNode<IVertexFormat>(parent), m_elementCount(elementCount),
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

class D3D12ShaderPipeline : public GraphicsDataNode<IShaderPipeline>
{
    friend class D3D12DataFactory;
    friend struct D3D12ShaderDataBinding;
    boo::ObjToken<IVertexFormat> m_vtxFmt;
    D3D12ShareableShader::Token m_vert;
    D3D12ShareableShader::Token m_pixel;

    D3D12ShaderPipeline(const boo::ObjToken<BaseGraphicsData>& parent,
                        D3D12Context* ctx, D3D12ShareableShader::Token&& vert,
                        D3D12ShareableShader::Token&& pixel, ID3DBlob* pipeline,
                        const boo::ObjToken<IVertexFormat>& vtxFmt,
                        BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                        ZTest depthTest, bool depthWrite, bool colorWrite,
                        bool alphaWrite, bool overwriteAlpha, CullMode culling)
    : GraphicsDataNode<IShaderPipeline>(parent), m_vtxFmt(vtxFmt),
      m_vert(std::move(vert)), m_pixel(std::move(pixel)),
      m_topology(PRIMITIVE_TABLE[int(prim)])
    {
        D3D12_CULL_MODE cullMode;
        switch (culling)
        {
        case CullMode::None:
        default:
            cullMode = D3D12_CULL_MODE_NONE;
            break;
        case CullMode::Backface:
            cullMode = D3D12_CULL_MODE_BACK;
            break;
        case CullMode::Frontface:
            cullMode = D3D12_CULL_MODE_FRONT;
            break;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = ctx->m_rs.Get();
        const auto& vBlob = m_vert.get().m_shader;
        const auto& pBlob = m_pixel.get().m_shader;
        desc.VS = {vBlob->GetBufferPointer(), vBlob->GetBufferSize()};
        desc.PS = {pBlob->GetBufferPointer(), pBlob->GetBufferSize()};
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        if (dstFac != BlendFactor::Zero)
        {
            desc.BlendState.RenderTarget[0].BlendEnable = true;
            if (srcFac == BlendFactor::Subtract || dstFac == BlendFactor::Subtract)
            {
                desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
                desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
                desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
                if (overwriteAlpha)
                {
                    desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
                    desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
                    desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
                }
                else
                {
                    desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
                    desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
                    desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_REV_SUBTRACT;
                }
            }
            else
            {
                desc.BlendState.RenderTarget[0].SrcBlend = BLEND_FACTOR_TABLE[int(srcFac)];
                desc.BlendState.RenderTarget[0].DestBlend = BLEND_FACTOR_TABLE[int(dstFac)];
                desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
                if (overwriteAlpha)
                {
                    desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
                    desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
                }
                else
                {
                    desc.BlendState.RenderTarget[0].SrcBlendAlpha = BLEND_FACTOR_TABLE[int(srcFac)];
                    desc.BlendState.RenderTarget[0].DestBlendAlpha = BLEND_FACTOR_TABLE[int(dstFac)];
                }
                desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            }
        }
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
                (colorWrite ? (D3D12_COLOR_WRITE_ENABLE_RED |
                               D3D12_COLOR_WRITE_ENABLE_GREEN |
                               D3D12_COLOR_WRITE_ENABLE_BLUE) : 0) |
                (alphaWrite ? D3D12_COLOR_WRITE_ENABLE_ALPHA : 0);
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.FrontCounterClockwise = TRUE;
        desc.RasterizerState.CullMode = cullMode;
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        switch (depthTest)
        {
        case ZTest::None:
        default:
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            break;
        case ZTest::LEqual:
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
            break;
        case ZTest::Greater:
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
            break;
        case ZTest::GEqual:
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            break;
        case ZTest::Equal:
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
            break;
        }
        desc.DepthStencilState.DepthEnable = depthTest != ZTest::None;
        if (!depthWrite)
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        D3D12VertexFormat* vfmt = vtxFmt.cast<D3D12VertexFormat>();
        desc.InputLayout.NumElements = vfmt->m_elementCount;
        desc.InputLayout.pInputElementDescs = vfmt->m_elements.get();
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = ctx->RGBATex2DFBViewDesc.Format;
        desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        desc.SampleDesc.Count = ctx->m_sampleCount;
        desc.SampleDesc.Quality = 0;
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
        return static_cast<D3D12GraphicsBufferD<BaseGraphicsData>*>(buf)->placeForGPU(ctx, gpuHeap, offset);
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
        const D3D12GraphicsBufferD<BaseGraphicsData>* cbuf =
                static_cast<const D3D12GraphicsBufferD<BaseGraphicsData>*>(buf);
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
        const D3D12GraphicsBufferD<BaseGraphicsData>* cbuf =
                static_cast<const D3D12GraphicsBufferD<BaseGraphicsData>*>(buf);
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
        const D3D12GraphicsBufferD<BaseGraphicsData>* cbuf =
                static_cast<const D3D12GraphicsBufferD<BaseGraphicsData>*>(buf);
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

static const struct RGBATex2DDepthViewDesc : D3D12_SHADER_RESOURCE_VIEW_DESC
{
    RGBATex2DDepthViewDesc()
    {
        Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        Texture2D = {UINT(0), UINT(1), UINT(0), 0.0f};
    }
} RGBATex2DDepthViewDesc;

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

static const struct Grey16Tex2DViewDesc : D3D12_SHADER_RESOURCE_VIEW_DESC
{
    Grey16Tex2DViewDesc()
    {
        Format = DXGI_FORMAT_R16_UNORM;
        ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        Texture2D = {UINT(0), UINT(-1), UINT(0), 0.0f};
    }
} Grey16Tex2DViewDesc;

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

static const struct Grey16Tex2DArrayViewDesc : D3D12_SHADER_RESOURCE_VIEW_DESC
{
    Grey16Tex2DArrayViewDesc()
    {
        Format = DXGI_FORMAT_R16_UNORM;
        ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        Texture2DArray = {UINT(0), UINT(1), 0, 0, UINT(0), 0.0f};
    }
} Grey16Tex2DArrayViewDesc;

static ID3D12Resource* GetTextureGPUResource(D3D12Context* ctx, const ITexture* tex, int idx, int bindIdx, bool depth,
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
        case TextureFormat::I16:
            descOut = Grey16Tex2DViewDesc;
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
        case TextureFormat::I16:
            descOut = Grey16Tex2DViewDesc;
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
        case TextureFormat::I16:
            descOut = Grey16Tex2DArrayViewDesc;
            break;
        default:break;
        }
        descOut.Texture2DArray.ArraySize = ctex->layers();
        return ctex->m_gpuTex.Get();
    }
    case TextureType::Render:
    {
        const D3D12TextureR* ctex = static_cast<const D3D12TextureR*>(tex);
        if (depth)
        {
            descOut = RGBATex2DDepthViewDesc;
            return ctex->m_depthBindTex[bindIdx].Get();
        }
        else
        {
            descOut = ctx->RGBATex2DFBViewDesc;
            return ctex->m_colorBindTex[bindIdx].Get();
        }
    }
    default: break;
    }
    return nullptr;
}

struct D3D12ShaderDataBinding : public GraphicsDataNode<IShaderDataBinding>
{
    boo::ObjToken<IShaderPipeline> m_pipeline;
    ComPtr<ID3D12Heap> m_gpuHeap;
    ComPtr<ID3D12DescriptorHeap> m_descHeap[2];
    boo::ObjToken<IGraphicsBuffer> m_vbuf;
    boo::ObjToken<IGraphicsBuffer> m_instVbuf;
    boo::ObjToken<IGraphicsBuffer> m_ibuf;
    std::vector<boo::ObjToken<IGraphicsBuffer>> m_ubufs;
    std::vector<std::pair<size_t,size_t>> m_ubufOffs;
    ID3D12Resource* m_knownViewHandles[2][8] = {};
    struct BindTex
    {
        boo::ObjToken<ITexture> tex;
        int idx;
        bool depth;
    };
    std::vector<BindTex> m_texs;
    D3D12_VERTEX_BUFFER_VIEW m_vboView[2][2] = {{},{}};
    D3D12_INDEX_BUFFER_VIEW m_iboView[2];
    size_t m_vertOffset, m_instOffset;

    D3D12ShaderDataBinding(const boo::ObjToken<BaseGraphicsData>& d,
                           D3D12Context* ctx,
                           const boo::ObjToken<IShaderPipeline>& pipeline,
                           const boo::ObjToken<IGraphicsBuffer>& vbuf,
                           const boo::ObjToken<IGraphicsBuffer>& instVbuf,
                           const boo::ObjToken<IGraphicsBuffer>& ibuf,
                           size_t ubufCount, const boo::ObjToken<IGraphicsBuffer>* ubufs,
                           const size_t* ubufOffs, const size_t* ubufSizes,
                           size_t texCount, const boo::ObjToken<ITexture>* texs,
                           const int* bindIdxs, const bool* bindDepth,
                           size_t baseVert, size_t baseInst)
    : GraphicsDataNode<IShaderDataBinding>(d),
      m_pipeline(pipeline),
      m_vbuf(vbuf),
      m_instVbuf(instVbuf),
      m_ibuf(ibuf)
    {
        D3D12ShaderPipeline* cpipeline = m_pipeline.cast<D3D12ShaderPipeline>();
        D3D12VertexFormat* vtxFmt = cpipeline->m_vtxFmt.cast<D3D12VertexFormat>();
        m_vertOffset = baseVert * vtxFmt->m_stride;
        m_instOffset = baseInst * vtxFmt->m_instStride;

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
            m_texs.push_back({texs[i], bindIdxs ? bindIdxs[i] : 0, bindDepth ? bindDepth[i] : false});
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
                GetBufferGPUResource(m_vbuf.get(), b, m_vboView[b][0], m_vertOffset);
            if (m_instVbuf)
                GetBufferGPUResource(m_instVbuf.get(), b, m_vboView[b][1], m_instOffset);
            if (m_ibuf)
                GetBufferGPUResource(m_ibuf.get(), b, m_iboView[b]);
            if (m_ubufOffs.size())
            {
                for (size_t i=0 ; i<MAX_UNIFORM_COUNT ; ++i)
                {
                    if (i<m_ubufs.size())
                    {
                        const std::pair<size_t,size_t>& offPair = m_ubufOffs[i];
                        D3D12_CONSTANT_BUFFER_VIEW_DESC viewDesc;
                        GetBufferGPUResource(m_ubufs[i].get(), b, viewDesc);
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
                    if (i<m_ubufs.size())
                    {
                        D3D12_CONSTANT_BUFFER_VIEW_DESC viewDesc;
                        GetBufferGPUResource(m_ubufs[i].get(), b, viewDesc);
                        viewDesc.SizeInBytes = (viewDesc.SizeInBytes + 255) & ~255;
                        ctx->m_dev->CreateConstantBufferView(&viewDesc, handle);
                    }
                    handle.Offset(1, incSz);
                }
            }
            for (size_t i=0 ; i<MAX_TEXTURE_COUNT ; ++i)
            {
                if (i<m_texs.size() && m_texs[i].tex)
                {
                    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
                    ID3D12Resource* res = GetTextureGPUResource(ctx, m_texs[i].tex.get(), b, m_texs[i].idx,
                                                                m_texs[i].depth, srvDesc);
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
            if (i<m_texs.size() && m_texs[i].tex)
            {
                if (m_texs[i].tex->type() == TextureType::Render)
                {
                    const D3D12TextureR* ctex = m_texs[i].tex.cast<D3D12TextureR>();
                    if (m_texs[i].depth)
                    {
                        ID3D12Resource* res = ctex->m_depthBindTex[m_texs[i].idx].Get();
                        if (res != m_knownViewHandles[b][i])
                        {
                            if (incSz == UINT(-1))
                            {
                                incSz = ctx->m_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                                heapStart = m_descHeap[b]->GetCPUDescriptorHandleForHeapStart();
                            }
                            m_knownViewHandles[b][i] = res;
                            ctx->m_dev->CreateShaderResourceView(res, &RGBATex2DDepthViewDesc,
                                CD3DX12_CPU_DESCRIPTOR_HANDLE(heapStart, MAX_UNIFORM_COUNT + i, incSz));
                        }
                    }
                    else
                    {
                        ID3D12Resource* res = ctex->m_colorBindTex[m_texs[i].idx].Get();
                        if (res != m_knownViewHandles[b][i])
                        {
                            if (incSz == UINT(-1))
                            {
                                incSz = ctx->m_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                                heapStart = m_descHeap[b]->GetCPUDescriptorHandleForHeapStart();
                            }
                            m_knownViewHandles[b][i] = res;
                            ctx->m_dev->CreateShaderResourceView(res, &ctx->RGBATex2DFBViewDesc,
                                CD3DX12_CPU_DESCRIPTOR_HANDLE(heapStart, MAX_UNIFORM_COUNT + i, incSz));
                        }
                    }
                }
            }
        }

        D3D12ShaderPipeline* pipeline = m_pipeline.cast<D3D12ShaderPipeline>();
        ID3D12DescriptorHeap* heap[] = {m_descHeap[b].Get()};
        list->SetDescriptorHeaps(1, heap);
        list->SetGraphicsRootDescriptorTable(0, m_descHeap[b]->GetGPUDescriptorHandleForHeapStart());
        list->SetPipelineState(pipeline->m_state.Get());
        list->IASetVertexBuffers(0, 2, m_vboView[b]);
        if (m_ibuf)
            list->IASetIndexBuffer(&m_iboView[b]);
        list->IASetPrimitiveTopology(pipeline->m_topology);
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

    void startRenderer();
    void stopRenderer();

    ~D3D12CommandQueue()
    {
        if (m_running) stopRenderer();
    }

    void setShaderDataBinding(const boo::ObjToken<IShaderDataBinding>& binding)
    {
        D3D12ShaderDataBinding* cbind = binding.cast<D3D12ShaderDataBinding>();
        cbind->bind(m_ctx, m_cmdList.Get(), m_fillBuf);
    }

    boo::ObjToken<ITextureR> m_boundTarget;
    void setRenderTarget(const boo::ObjToken<ITextureR>& target)
    {
        D3D12TextureR* ctarget = target.cast<D3D12TextureR>();

        m_cmdList->OMSetRenderTargets(1, &ctarget->m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
                                      false, &ctarget->m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

        m_boundTarget = target;
    }

    void setViewport(const SWindowRect& rect, float znear, float zfar)
    {
        if (m_boundTarget)
        {
            D3D12TextureR* ctarget = m_boundTarget.cast<D3D12TextureR>();
            D3D12_VIEWPORT vp = {FLOAT(rect.location[0]), FLOAT(ctarget->m_height - rect.location[1] - rect.size[1]),
                                 FLOAT(rect.size[0]), FLOAT(rect.size[1]), 1.f - zfar, 1.f - znear};
            m_cmdList->RSSetViewports(1, &vp);
        }
    }

    void setScissor(const SWindowRect& rect)
    {
        if (m_boundTarget)
        {
            D3D12TextureR* ctarget = m_boundTarget.cast<D3D12TextureR>();
            D3D12_RECT d3drect = {LONG(rect.location[0]), LONG(ctarget->m_height - rect.location[1] - rect.size[1]),
                                  LONG(rect.location[0] + rect.size[0]), LONG(ctarget->m_height - rect.location[1])};
            m_cmdList->RSSetScissorRects(1, &d3drect);
        }
    }

    std::unordered_map<D3D12TextureR*, std::pair<size_t, size_t>> m_texResizes;
    void resizeRenderTexture(const boo::ObjToken<ITextureR>& tex, size_t width, size_t height)
    {
        D3D12TextureR* ctex = tex.cast<D3D12TextureR>();
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
        D3D12TextureR* ctarget = m_boundTarget.cast<D3D12TextureR>();
        if (render)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE handle(ctarget->m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
            m_cmdList->ClearRenderTargetView(handle, m_clearColor, 0, nullptr);
        }
        if (depth)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE handle(ctarget->m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
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

    void resolveBindTexture(const boo::ObjToken<ITextureR>& texture,
                            const SWindowRect& rect, bool tlOrigin,
                            int bindIdx, bool color, bool depth, bool clearDepth)
    {
        D3D12TextureR* tex = texture.cast<D3D12TextureR>();

        if (color && tex->m_colorBindCount)
        {
            D3D12_RESOURCE_BARRIER copySetup[] =
            {
                CD3DX12_RESOURCE_BARRIER::Transition(tex->m_colorTex.Get(),
                    D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(tex->m_colorBindTex[bindIdx].Get(),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)
            };
            m_cmdList->ResourceBarrier(2, copySetup);

            if (tex->m_samples > 1)
            {
                m_cmdList->CopyResource(tex->m_colorBindTex[bindIdx].Get(), tex->m_colorTex.Get());
            }
            else
            {
                SWindowRect intersectRect = rect.intersect(SWindowRect(0, 0, tex->m_width, tex->m_height));
                int y = tlOrigin ? intersectRect.location[1] : (tex->m_height - intersectRect.size[1] - intersectRect.location[1]);
                D3D12_BOX box = {UINT(intersectRect.location[0]), UINT(y), 0,
                                 UINT(intersectRect.location[0] + intersectRect.size[0]), UINT(y + intersectRect.size[1]), 1};

                D3D12_TEXTURE_COPY_LOCATION dst = {tex->m_colorBindTex[bindIdx].Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
                dst.SubresourceIndex = 0;
                D3D12_TEXTURE_COPY_LOCATION src = {tex->m_colorTex.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
                src.SubresourceIndex = 0;
                m_cmdList->CopyTextureRegion(&dst, box.left, box.top, 0, &src, &box);
            }

            D3D12_RESOURCE_BARRIER copyTeardown[] =
            {
                CD3DX12_RESOURCE_BARRIER::Transition(tex->m_colorTex.Get(),
                    D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
                CD3DX12_RESOURCE_BARRIER::Transition(tex->m_colorBindTex[bindIdx].Get(),
                    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
            };
            m_cmdList->ResourceBarrier(2, copyTeardown);
        }
        if (depth && tex->m_depthBindCount)
        {
            D3D12_RESOURCE_BARRIER copySetup[] =
            {
                CD3DX12_RESOURCE_BARRIER::Transition(tex->m_depthTex.Get(),
                    D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(tex->m_depthBindTex[bindIdx].Get(),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)
            };
            m_cmdList->ResourceBarrier(2, copySetup);

            m_cmdList->CopyResource(tex->m_depthBindTex[bindIdx].Get(), tex->m_depthTex.Get());

            D3D12_RESOURCE_BARRIER copyTeardown[] =
            {
                CD3DX12_RESOURCE_BARRIER::Transition(tex->m_depthTex.Get(),
                    D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE),
                CD3DX12_RESOURCE_BARRIER::Transition(tex->m_depthBindTex[bindIdx].Get(),
                    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
            };
            m_cmdList->ResourceBarrier(2, copyTeardown);
        }

        if (clearDepth)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE handle(tex->m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
            m_cmdList->ClearDepthStencilView(handle, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
        }
    }

    bool m_doPresent = false;
    void resolveDisplay(const boo::ObjToken<ITextureR>& source);

    UINT64 m_submittedFenceVal = 0;
    void execute();
};

D3D12TextureR::~D3D12TextureR()
{
    if (m_q->m_boundTarget.get() == this)
        m_q->m_boundTarget.reset();
}

template <class DataCls>
void D3D12GraphicsBufferD<DataCls>::update(int b)
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

template <class DataCls>
void D3D12GraphicsBufferD<DataCls>::load(const void* data, size_t sz)
{
    size_t bufSz = std::min(sz, m_cpuSz);
    memcpy(m_cpuBuf.get(), data, bufSz);
    m_validSlots = 0;
}
template <class DataCls>
void* D3D12GraphicsBufferD<DataCls>::map(size_t sz)
{
    if (sz > m_cpuSz)
        return nullptr;
    return m_cpuBuf.get();
}
template <class DataCls>
void D3D12GraphicsBufferD<DataCls>::unmap()
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

class D3D12DataFactory : public ID3DDataFactory, public GraphicsDataFactoryHead
{
    friend struct D3D12CommandQueue;
    IGraphicsContext* m_parent;
    struct D3D12Context* m_ctx;
    std::unordered_map<uint64_t, std::unique_ptr<D3D12ShareableShader>> m_sharedShaders;
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
    D3D12DataFactory(IGraphicsContext* parent, D3D12Context* ctx)
    : m_parent(parent), m_ctx(ctx)
    {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qLevels = {};
        qLevels.Format = m_ctx->RGBATex2DFBViewDesc.Format;
        qLevels.SampleCount = m_ctx->m_sampleCount;
        while (SUCCEEDED(ctx->m_dev->CheckFeatureSupport
                         (D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qLevels, sizeof(qLevels))) &&
               !qLevels.NumQualityLevels)
            qLevels.SampleCount = flp2(qLevels.SampleCount - 1);
        m_ctx->m_sampleCount = qLevels.SampleCount;

        m_ctx->m_anisotropy = std::min(m_ctx->m_anisotropy, uint32_t(16));

        CD3DX12_DESCRIPTOR_RANGE ranges[] =
        {
            {D3D12_DESCRIPTOR_RANGE_TYPE_CBV, MAX_UNIFORM_COUNT, 0},
            {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, MAX_TEXTURE_COUNT, 0}
        };
        CD3DX12_ROOT_PARAMETER rootParms[1];
        rootParms[0].InitAsDescriptorTable(2, ranges);

        ComPtr<ID3DBlob> rsOutBlob;
        ComPtr<ID3DBlob> rsErrorBlob;

        D3D12_STATIC_SAMPLER_DESC samplers[] =
        {
            CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 0.f, m_ctx->m_anisotropy),
            CD3DX12_STATIC_SAMPLER_DESC(1, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, 0.f, m_ctx->m_anisotropy),
            CD3DX12_STATIC_SAMPLER_DESC(2, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0.f, m_ctx->m_anisotropy),
            CD3DX12_STATIC_SAMPLER_DESC(3, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0.f, m_ctx->m_anisotropy)
        };

        ThrowIfFailed(D3D12SerializeRootSignaturePROC(
            &CD3DX12_ROOT_SIGNATURE_DESC(1, rootParms, 4, samplers,
                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT),
            D3D_ROOT_SIGNATURE_VERSION_1, &rsOutBlob, &rsErrorBlob));

        ThrowIfFailed(ctx->m_dev->CreateRootSignature(0, rsOutBlob->GetBufferPointer(),
            rsOutBlob->GetBufferSize(), __uuidof(ID3D12RootSignature), &ctx->m_rs));
    }

    Platform platform() const {return Platform::D3D12;}
    const SystemChar* platformName() const {return _S("D3D12");}

    class Context : public ID3DDataFactory::Context
    {
        friend class D3D12DataFactory;
        D3D12DataFactory& m_parent;
        boo::ObjToken<BaseGraphicsData> m_data;
        Context(D3D12DataFactory& parent)
        : m_parent(parent), m_data(new D3D12Data(parent)) {}
    public:
        Platform platform() const {return Platform::D3D12;}
        const SystemChar* platformName() const {return _S("D3D12");}

        boo::ObjToken<IGraphicsBufferS>
        newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count)
        {
            return {new D3D12GraphicsBufferS(m_data, use, m_parent.m_ctx, data, stride, count)};
        }

        boo::ObjToken<IGraphicsBufferD>
        newDynamicBuffer(BufferUse use, size_t stride, size_t count)
        {
            D3D12CommandQueue* q = static_cast<D3D12CommandQueue*>(m_parent.m_parent->getCommandQueue());
            return {new D3D12GraphicsBufferD<BaseGraphicsData>(m_data, q, use, m_parent.m_ctx, stride, count)};
        }

        boo::ObjToken<ITextureS>
        newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                         TextureClampMode clampMode, const void* data, size_t sz)
        {
            return {new D3D12TextureS(m_data, m_parent.m_ctx, width, height, mips, fmt, data, sz)};
        }

        boo::ObjToken<ITextureSA>
        newStaticArrayTexture(size_t width, size_t height, size_t layers, size_t mips,
                              TextureFormat fmt, TextureClampMode clampMode, const void* data, size_t sz)
        {
            return {new D3D12TextureSA(m_data, m_parent.m_ctx, width, height, layers, mips, fmt, data, sz)};
        }

        boo::ObjToken<ITextureD>
        newDynamicTexture(size_t width, size_t height, TextureFormat fmt, TextureClampMode clampMode)
        {
            D3D12CommandQueue* q = static_cast<D3D12CommandQueue*>(m_parent.m_parent->getCommandQueue());
            return {new D3D12TextureD(m_data, q, m_parent.m_ctx, width, height, fmt)};
        }

        boo::ObjToken<ITextureR>
        newRenderTexture(size_t width, size_t height, TextureClampMode clampMode,
                         size_t colorBindCount, size_t depthBindCount)
        {
            D3D12CommandQueue* q = static_cast<D3D12CommandQueue*>(m_parent.m_parent->getCommandQueue());
            return {new D3D12TextureR(m_data, m_parent.m_ctx, q, width, height, m_parent.m_ctx->m_sampleCount,
                                      colorBindCount, depthBindCount)};
        }

        boo::ObjToken<IVertexFormat>
        newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements,
                        size_t baseVert, size_t baseInst)
        {
            return {new struct D3D12VertexFormat(m_data, elementCount, elements)};
        }

#if _DEBUG && 0
#define BOO_D3DCOMPILE_FLAG D3DCOMPILE_DEBUG | D3DCOMPILE_OPTIMIZATION_LEVEL0
#else
#define BOO_D3DCOMPILE_FLAG D3DCOMPILE_OPTIMIZATION_LEVEL3
#endif

        static uint64_t CompileVert(ComPtr<ID3DBlob>& vertBlobOut, const char* vertSource, uint64_t srcKey,
                                    D3D12DataFactory& factory)
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
                                    D3D12DataFactory& factory)
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

            D3D12ShareableShader::Token vertShader;
            D3D12ShareableShader::Token fragShader;
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

                auto it =
                m_parent.m_sharedShaders.emplace(std::make_pair(binHashes[0],
                    std::make_unique<D3D12ShareableShader>(m_parent, srcHashes[0], binHashes[0],
                                                           std::move(vertBlob)))).first;
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
                if (fragBlobOut)
                    fragBlob = *fragBlobOut;
                else
                    binHashes[1] = CompileFrag(fragBlob, fragSource, srcHashes[1], m_parent);

                auto it =
                m_parent.m_sharedShaders.emplace(std::make_pair(binHashes[1],
                    std::make_unique<D3D12ShareableShader>(m_parent, srcHashes[1], binHashes[1],
                                                           std::move(fragBlob)))).first;
                fragShader = it->second->lock();
            }

            ID3DBlob* pipeline = pipelineBlob ? pipelineBlob->Get() : nullptr;
            D3D12ShaderPipeline* retval = new D3D12ShaderPipeline(
                m_data, m_parent.m_ctx, std::move(vertShader), std::move(fragShader),
                pipeline, vtxFmt, srcFac, dstFac, prim, depthTest, depthWrite, colorWrite,
                alphaWrite, overwriteAlpha, culling);
            if (pipelineBlob && !*pipelineBlob)
                retval->m_state->GetCachedBlob(&*pipelineBlob);
            return retval;
        }

        boo::ObjToken<IShaderDataBinding> newShaderDataBinding(
                const boo::ObjToken<IShaderPipeline>& pipeline,
                const boo::ObjToken<IVertexFormat>& vtxFormat,
                const boo::ObjToken<IGraphicsBuffer>& vbuf,
                const boo::ObjToken<IGraphicsBuffer>& instVbuf,
                const boo::ObjToken<IGraphicsBuffer>& ibuf,
                size_t ubufCount, const boo::ObjToken<IGraphicsBuffer>* ubufs, const PipelineStage* ubufStages,
                const size_t* ubufOffs, const size_t* ubufSizes,
                size_t texCount, const boo::ObjToken<ITexture>* texs,
                const int* bindIdxs, const bool* bindDepth,
                size_t baseVert, size_t baseInst)
        {
            return {new D3D12ShaderDataBinding(m_data, m_parent.m_ctx, pipeline, vbuf, instVbuf, ibuf,
                                               ubufCount, ubufs, ubufOffs, ubufSizes, texCount, texs,
                                               bindIdxs, bindDepth, baseVert, baseInst)};
        }
    };

    boo::ObjToken<IGraphicsBufferD> newPoolBuffer(BufferUse use, size_t stride, size_t count)
    {
        D3D12CommandQueue* q = static_cast<D3D12CommandQueue*>(m_parent->getCommandQueue());
        boo::ObjToken<BaseGraphicsPool> pool(new D3D12Pool(*this));
        D3D12Pool* cpool = pool.cast<D3D12Pool>();
        D3D12GraphicsBufferD<BaseGraphicsPool>* retval =
                new D3D12GraphicsBufferD<BaseGraphicsPool>(pool, q, use, m_ctx, stride, count);

        /* Gather resource descriptions */
        D3D12_RESOURCE_DESC bufDescs[2];
        bufDescs[0] = retval->m_bufs[0]->GetDesc();
        bufDescs[1] = retval->m_bufs[1]->GetDesc();

        /* Create heap */
        D3D12_RESOURCE_ALLOCATION_INFO bufAllocInfo =
            m_ctx->m_dev->GetResourceAllocationInfo(0, 2, bufDescs);
        ThrowIfFailed(m_ctx->m_dev->CreateHeap(&CD3DX12_HEAP_DESC(bufAllocInfo,
            D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS),
            __uuidof(ID3D12Heap), &cpool->m_bufHeap));

        /* Place resources */
        PlaceBufferForGPU(retval, m_ctx, cpool->m_bufHeap.Get(), 0);

        return {retval};
    }

    void commitTransaction(const FactoryCommitFunc& trans)
    {
        D3D12DataFactory::Context ctx(*this);
        if (!trans(ctx))
            return;

        D3D12Data* data = ctx.m_data.cast<D3D12Data>();

        /* Gather resource descriptions */
        std::vector<D3D12_RESOURCE_DESC> bufDescs;
        bufDescs.reserve(data->countForward<IGraphicsBufferS>() +
                         data->countForward<IGraphicsBufferD>() * 2);

        std::vector<D3D12_RESOURCE_DESC> texDescs;
        texDescs.reserve(data->countForward<ITextureS>() +
                         data->countForward<ITextureSA>() +
                         data->countForward<ITextureD>() * 2);

        if (data->m_SBufs)
            for (IGraphicsBufferS& buf : *data->m_SBufs)
                bufDescs.push_back(static_cast<D3D12GraphicsBufferS&>(buf).m_gpuDesc);

        if (data->m_DBufs)
            for (IGraphicsBufferD& buf : *data->m_DBufs)
            {
                bufDescs.push_back(static_cast<D3D12GraphicsBufferD<BaseGraphicsData>&>(buf).m_bufs[0]->GetDesc());
                bufDescs.push_back(static_cast<D3D12GraphicsBufferD<BaseGraphicsData>&>(buf).m_bufs[1]->GetDesc());
            }

        if (data->m_STexs)
            for (ITextureS& tex : *data->m_STexs)
                texDescs.push_back(static_cast<D3D12TextureS&>(tex).m_gpuDesc);

        if (data->m_SATexs)
            for (ITextureSA& tex : *data->m_SATexs)
                texDescs.push_back(static_cast<D3D12TextureSA&>(tex).m_gpuDesc);

        if (data->m_DTexs)
            for (ITextureD& tex : *data->m_DTexs)
            {
                texDescs.push_back(static_cast<D3D12TextureD&>(tex).m_gpuDesc);
                texDescs.push_back(static_cast<D3D12TextureD&>(tex).m_gpuDesc);
            }

        /* Create heap */
        if (bufDescs.size())
        {
            D3D12_RESOURCE_ALLOCATION_INFO bufAllocInfo =
                m_ctx->m_dev->GetResourceAllocationInfo(0, bufDescs.size(), bufDescs.data());
            ThrowIfFailed(m_ctx->m_dev->CreateHeap(&CD3DX12_HEAP_DESC(bufAllocInfo,
                D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS),
                __uuidof(ID3D12Heap), &data->m_bufHeap));
        }
        if (texDescs.size())
        {
            D3D12_RESOURCE_ALLOCATION_INFO texAllocInfo =
                m_ctx->m_dev->GetResourceAllocationInfo(0, texDescs.size(), texDescs.data());
            ThrowIfFailed(m_ctx->m_dev->CreateHeap(&CD3DX12_HEAP_DESC(texAllocInfo,
                D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES),
                __uuidof(ID3D12Heap), &data->m_texHeap));
        }
        ID3D12Heap* bufHeap = data->m_bufHeap.Get();
        ID3D12Heap* texHeap = data->m_texHeap.Get();

        /* Place resources */
        UINT64 offsetBuf = 0;
        if (data->m_SBufs)
            for (IGraphicsBufferS& buf : *data->m_SBufs)
                offsetBuf = PlaceBufferForGPU(&buf, m_ctx, bufHeap, offsetBuf);

        if (data->m_DBufs)
            for (IGraphicsBufferD& buf : *data->m_DBufs)
                offsetBuf = PlaceBufferForGPU(&buf, m_ctx, bufHeap, offsetBuf);

        UINT64 offsetTex = 0;
        if (data->m_STexs)
            for (ITextureS& tex : *data->m_STexs)
                offsetTex = PlaceTextureForGPU(&tex, m_ctx, texHeap, offsetTex);

        if (data->m_SATexs)
            for (ITextureSA& tex : *data->m_SATexs)
                offsetTex = PlaceTextureForGPU(&tex, m_ctx, texHeap, offsetTex);

        if (data->m_DTexs)
            for (ITextureD& tex : *data->m_DTexs)
                offsetTex = PlaceTextureForGPU(&tex, m_ctx, texHeap, offsetTex);

        /* Execute static uploads */
        ThrowIfFailed(m_ctx->m_loadlist->Close());
        ID3D12CommandList* list[] = {m_ctx->m_loadlist.Get()};
        m_ctx->m_loadq->ExecuteCommandLists(1, list);
        ++m_ctx->m_loadfenceval;
        ThrowIfFailed(m_ctx->m_loadq->Signal(m_ctx->m_loadfence.Get(), m_ctx->m_loadfenceval));

        /* Commit data bindings (create descriptor heaps) */
        if (data->m_SBinds)
            for (IShaderDataBinding& bind : *data->m_SBinds)
                static_cast<D3D12ShaderDataBinding&>(bind).commit(m_ctx);

        /* Block handle return until data is ready on GPU */
        WaitForLoadList(m_ctx);

        /* Reset allocator and list */
        ThrowIfFailed(m_ctx->m_loadqalloc->Reset());
        ThrowIfFailed(m_ctx->m_loadlist->Reset(m_ctx->m_loadqalloc.Get(), nullptr));

        /* Delete static upload heaps */
        if (data->m_SBufs)
            for (IGraphicsBufferS& buf : *data->m_SBufs)
                static_cast<D3D12GraphicsBufferS&>(buf).m_buf.Reset();

        if (data->m_STexs)
            for (ITextureS& tex : *data->m_STexs)
                static_cast<D3D12TextureS&>(tex).m_tex.Reset();

        if (data->m_SATexs)
            for (ITextureSA& tex : *data->m_SATexs)
                static_cast<D3D12TextureSA&>(tex).m_tex.Reset();
    }

    void _unregisterShareableShader(uint64_t srcKey, uint64_t binKey)
    {
        if (srcKey)
            m_sourceToBinary.erase(srcKey);
        m_sharedShaders.erase(binKey);
    }

    void setDisplayGamma(float gamma)
    {
        if (m_ctx->RGBATex2DFBViewDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
            m_gamma = gamma * 2.2f;
        else
            m_gamma = gamma;
        if (m_gamma != 1.f)
            UpdateGammaLUT(m_gammaLUT.get(), m_gamma);
    }
};

void D3D12CommandQueue::startRenderer()
{
    static_cast<D3D12DataFactory*>(m_parent->getDataFactory())->SetupGammaResources();
}

void D3D12CommandQueue::stopRenderer()
{
    m_running = false;
    if (m_fence->GetCompletedValue() < m_submittedFenceVal)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(m_submittedFenceVal, m_renderFenceHandle));
        WaitForSingleObject(m_renderFenceHandle, INFINITE);
    }
    /*
    D3D12DataFactory* dataFactory = static_cast<D3D12DataFactory*>(m_parent->getDataFactory());
    dataFactory->m_gammaShader.reset();
    dataFactory->m_gammaLUT.reset();
    dataFactory->m_gammaVBO.reset();
    dataFactory->m_gammaVFMT.reset();
    dataFactory->m_gammaBinding.reset();
    */
}

void D3D12CommandQueue::resolveDisplay(const boo::ObjToken<ITextureR>& source)
{
    D3D12TextureR* csource = source.cast<D3D12TextureR>();
#ifndef NDEBUG
    if (!csource->m_colorBindCount)
        Log.report(logvisor::Fatal,
                   "texture provided to resolveDisplay() must have at least 1 color binding");
#endif

    if (m_windowCtx->m_needsResize)
    {
        UINT nodeMasks[] = {0,0};
        IUnknown* const queues[] = {m_ctx->m_q.Get(), m_ctx->m_q.Get()};
        m_windowCtx->m_swapChain->ResizeBuffers1(2, m_windowCtx->width, m_windowCtx->height,
            m_ctx->RGBATex2DFBViewDesc.Format, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH, nodeMasks, queues);
        m_windowCtx->m_backBuf = m_windowCtx->m_swapChain->GetCurrentBackBufferIndex();
        m_windowCtx->m_rtvHeaps.clear();
        m_windowCtx->m_needsResize = false;
        return;
    }

    ComPtr<ID3D12Resource> dest;
    ThrowIfFailed(m_windowCtx->m_swapChain->GetBuffer(m_windowCtx->m_backBuf, __uuidof(ID3D12Resource), &dest));

    ID3D12Resource* src = csource->m_colorTex.Get();
    D3D12_RESOURCE_STATES srcState = D3D12_RESOURCE_STATE_COPY_SOURCE;
    if (m_boundTarget.get() == csource)
        srcState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    D3D12DataFactory* dataFactory = static_cast<D3D12DataFactory*>(m_parent->getDataFactory());
    if (dataFactory->m_gamma != 1.f)
    {
        SWindowRect rect(0, 0, csource->m_width, csource->m_height);
        resolveBindTexture(source, rect, true, 0, true, false, false);

        auto search = m_windowCtx->m_rtvHeaps.find(dest.Get());
        if (search == m_windowCtx->m_rtvHeaps.end())
        {
            ComPtr<ID3D12DescriptorHeap> rtvHeap;
            D3D12_DESCRIPTOR_HEAP_DESC rtvdesc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1};
            ThrowIfFailed(m_ctx->m_dev->CreateDescriptorHeap(&rtvdesc, __uuidof(ID3D12DescriptorHeap), &rtvHeap));

            D3D12_RENDER_TARGET_VIEW_DESC rtvvdesc = {m_ctx->RGBATex2DFBViewDesc.Format, D3D12_RTV_DIMENSION_TEXTURE2D};
            m_ctx->m_dev->CreateRenderTargetView(dest.Get(), &rtvvdesc, rtvHeap->GetCPUDescriptorHandleForHeapStart());

            search = m_windowCtx->m_rtvHeaps.insert(std::make_pair(dest.Get(), rtvHeap)).first;
        }

        D3D12_RESOURCE_BARRIER gammaSetup[] =
        {
            CD3DX12_RESOURCE_BARRIER::Transition(dest.Get(),
                D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)
        };
        m_cmdList->ResourceBarrier(1, gammaSetup);

        m_cmdList->OMSetRenderTargets(1, &search->second->GetCPUDescriptorHandleForHeapStart(), false, nullptr);

        D3D12ShaderDataBinding* gammaBinding = dataFactory->m_gammaBinding.cast<D3D12ShaderDataBinding>();
        gammaBinding->m_texs[0].tex = source.get();
        gammaBinding->bind(m_ctx, m_cmdList.Get(), m_fillBuf);
        m_cmdList->DrawInstanced(4, 1, 0, 0);
        gammaBinding->m_texs[0].tex.reset();

        D3D12_RESOURCE_BARRIER gammaTeardown[] =
        {
            CD3DX12_RESOURCE_BARRIER::Transition(dest.Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)
        };
        m_cmdList->ResourceBarrier(1, gammaTeardown);
    }
    else
    {
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

            m_cmdList->ResolveSubresource(dest.Get(), 0, src, 0, m_ctx->RGBATex2DFBViewDesc.Format);

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
    }

    m_doPresent = true;
}

void D3D12CommandQueue::execute()
{
    if (!m_running)
        return;

    /* Stage dynamic uploads */
    D3D12DataFactory* gfxF = static_cast<D3D12DataFactory*>(m_parent->getDataFactory());
    std::unique_lock<std::recursive_mutex> datalk(gfxF->m_dataMutex);
    if (gfxF->m_dataHead)
    {
        for (BaseGraphicsData& d : *gfxF->m_dataHead)
        {
            if (d.m_DBufs)
                for (IGraphicsBufferD& b : *d.m_DBufs)
                    static_cast<D3D12GraphicsBufferD<BaseGraphicsData>&>(b).update(m_fillBuf);
            if (d.m_DTexs)
                for (ITextureD& t : *d.m_DTexs)
                    static_cast<D3D12TextureD&>(t).update(m_fillBuf);
        }
    }
    if (gfxF->m_poolHead)
    {
        for (BaseGraphicsPool& p : *gfxF->m_poolHead)
        {
            if (p.m_DBufs)
                for (IGraphicsBufferD& b : *p.m_DBufs)
                    static_cast<D3D12GraphicsBufferD<BaseGraphicsData>&>(b).update(m_fillBuf);
        }
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

IGraphicsDataFactory* _NewD3D12DataFactory(D3D12Context* ctx, IGraphicsContext* parent)
{
    return new D3D12DataFactory(parent, ctx);
}

}

#endif // _WIN32_WINNT_WIN10
