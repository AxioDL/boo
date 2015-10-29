#ifndef IGFXDATAFACTORY_HPP
#define IGFXDATAFACTORY_HPP

#include <memory>
#include <stdint.h>

namespace boo
{

struct IGraphicsBuffer
{
    bool dynamic() const {return m_dynamic;}
protected:
    bool m_dynamic;
    IGraphicsBuffer(bool dynamic) : m_dynamic(dynamic) {}
};

/** Static resource buffer for verts, indices, uniform constants */
struct IGraphicsBufferS : IGraphicsBuffer
{
protected:
    IGraphicsBufferS() : IGraphicsBuffer(false) {}
};

/** Dynamic resource buffer for verts, indices, uniform constants */
struct IGraphicsBufferD : IGraphicsBuffer
{
    virtual void load(const void* data, size_t sz)=0;
    virtual void* map(size_t sz)=0;
    virtual void unmap()=0;
protected:
    IGraphicsBufferD() : IGraphicsBuffer(true) {}
};

struct ITexture
{
    bool dynamic() const {return m_dynamic;}
protected:
    bool m_dynamic;
    ITexture(bool dynamic) : m_dynamic(dynamic) {}
};

/** Static resource buffer for textures */
struct ITextureS : ITexture
{
protected:
    ITextureS() : ITexture(false) {}
};

/** Dynamic resource buffer for textures */
struct ITextureD : ITexture
{
    virtual void load(const void* data, size_t sz)=0;
    virtual void* map(size_t sz)=0;
    virtual void unmap()=0;
protected:
    ITextureD() : ITexture(true) {}
};

/** Opaque token for referencing a complete graphics pipeline state necessary
 *  to rasterize geometry (shaders and blending modes mainly) */
struct IShaderPipeline {};

/** Opaque token serving as indirection table for shader resources
 *  and IShaderPipeline reference. Each renderable surface-material holds one
 *  as a reference */
struct IShaderDataBinding {};

/** Opaque token for maintaining ownership of factory-created resources
 *  deletion of this token triggers mass-deallocation of the factory's
 *  resource batch. */
struct IGraphicsData
{
    virtual ~IGraphicsData() {}
};

/** Factory object for creating batches of resources as an IGraphicsData token */
struct IGraphicsDataFactory
{
    virtual ~IGraphicsDataFactory() {}

    enum Platform
    {
        PlatformNull,
        PlatformOGLES3,
        PlatformD3D11,
        PlatformD3D12,
        PlatformMetal,
        PlatformGX,
        PlatformGX2
    };
    virtual Platform platform() const=0;
    virtual const char* platformName() const=0;

    enum BufferUse
    {
        BufferUseNull,
        BufferUseVertex,
        BufferUseIndex,
        BufferUseUniform
    };
    virtual const IGraphicsBufferS*
    newStaticBuffer(BufferUse use, const void* data, size_t sz)=0;
    virtual IGraphicsBufferD*
    newDynamicBuffer(BufferUse use)=0;

    struct VertexElementDescriptor
    {
        const IGraphicsBuffer* vertBuffer = nullptr;
        const IGraphicsBuffer* indexBuffer = nullptr;
        enum VertexSemantic
        {
            VertexSemanticPosition,
            VertexSemanticNormal,
            VertexSemanticColor,
            VertexSemanticUV,
            VertexSemanticWeight
        } semantic;
    };

    enum TextureFormat
    {
        TextureFormatRGBA8,
        TextureFormatDXT1,
        TextureFormatPVRTC4
    };
    virtual const ITextureS*
    newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                     const void* data, size_t sz)=0;
    virtual ITextureD*
    newDynamicTexture(size_t width, size_t height, TextureFormat fmt)=0;

    enum BlendFactor
    {
        BlendFactorZero,
        BlendFactorOne,
        BlendFactorSrcColor,
        BlendFactorInvSrcColor,
        BlendFactorDstColor,
        BlendFactorInvDstColor,
        BlendFactorSrcAlpha,
        BlendFactorInvSrcAlpha,
        BlendFactorDstAlpha,
        BlendFactorInvDstAlpha
    };

    virtual const IShaderDataBinding*
    newShaderDataBinding(const IShaderPipeline* pipeline,
                         size_t bufCount, const IGraphicsBuffer** bufs,
                         size_t texCount, const ITexture** texs)=0;

    virtual void reset()=0;
    virtual std::unique_ptr<IGraphicsData> commit()=0;
};

}

#endif // IGFXDATAFACTORY_HPP
