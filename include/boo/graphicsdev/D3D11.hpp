#ifndef GDEV_D3D11_HPP
#define GDEV_D3D11_HPP

#include "IGraphicsDataFactory.hpp"

namespace boo
{

class D3D11DataFactory : public IGraphicsDataFactory
{
    IGraphicsContext* m_parent;
    IGraphicsData* m_deferredData = nullptr;
    std::unordered_set<IGraphicsData*> m_committedData;
public:
    D3D11DataFactory(IGraphicsContext* parent);
    ~D3D11DataFactory() {}

    Platform platform() const {return PlatformD3D11;}
    const char* platformName() const {return "Direct 3D 11";}

    const IGraphicsBufferS* newStaticBuffer(BufferUse use, const void* data, size_t sz);
    IGraphicsBufferD* newDynamicBuffer(BufferUse use);

    const ITextureS* newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
        const void* data, size_t sz);
    ITextureD* newDynamicTexture(size_t width, size_t height, TextureFormat fmt);

    const IVertexFormat* newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements);

    const IShaderPipeline* newShaderPipeline(const char* vertSource, const char* fragSource,
        size_t texCount, const char** texNames,
        BlendFactor srcFac, BlendFactor dstFac,
        bool depthTest, bool depthWrite, bool backfaceCulling);

    const IShaderDataBinding*
        newShaderDataBinding(const IShaderPipeline* pipeline,
            const IVertexFormat* vtxFormat,
            const IGraphicsBuffer* vbo, const IGraphicsBuffer* ebo,
            size_t ubufCount, const IGraphicsBuffer** ubufs,
            size_t texCount, const ITexture** texs);

    void reset();
    IGraphicsData* commit();
    void destroyData(IGraphicsData*);
    void destroyAllData();
};

}

#endif // GDEV_D3D11_HPP
