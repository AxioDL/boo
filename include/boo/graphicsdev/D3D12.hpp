#ifndef GDEV_D3D12_HPP
#define GDEV_D3D12_HPP

#include "IGraphicsDataFactory.hpp"
#include "IGraphicsCommandQueue.hpp"
#include "boo/IGraphicsContext.hpp"
#include <d3d12.h>
#include <vector>
#include <unordered_set>

#include <wrl/client.h>
template <class T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

namespace boo
{

class D3D12DataFactory : public IGraphicsDataFactory
{
    IGraphicsContext* m_parent;
    IGraphicsData* m_deferredData = nullptr;
    struct D3D12Context* m_ctx;
    std::unordered_set<IGraphicsData*> m_committedData;
public:
    D3D12DataFactory(IGraphicsContext* parent, D3D12Context* ctx);
    ~D3D12DataFactory() {}

    Platform platform() const {return PlatformD3D12;}
    const char* platformName() const {return "Direct 3D 12";}

    const IGraphicsBufferS* newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count);
    IGraphicsBufferD* newDynamicBuffer(BufferUse use, size_t stride, size_t count);

    const ITextureS* newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                      const void* data, size_t sz);
    ITextureD* newDynamicTexture(size_t width, size_t height, TextureFormat fmt);

    const IVertexFormat* newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements);

    const IShaderPipeline* newShaderPipeline(const char* vertSource, const char* fragSource,
                                             ComPtr<ID3DBlob>& vertBlobOut, ComPtr<ID3DBlob>& fragBlobOut,
                                             const IVertexFormat* vtxFmt,
                                             BlendFactor srcFac, BlendFactor dstFac,
                                             bool depthTest, bool depthWrite, bool backfaceCulling);

    const IShaderDataBinding*
    newShaderDataBinding(IShaderPipeline* pipeline,
                         IVertexFormat* vtxFormat,
                         IGraphicsBuffer* vbo, IGraphicsBuffer* ebo,
                         size_t ubufCount, IGraphicsBuffer** ubufs,
                         size_t texCount, ITexture** texs);

    void reset();
    IGraphicsData* commit();
    void destroyData(IGraphicsData* data);
    void destroyAllData();
};

}

#endif // GDEV_D3D12_HPP
