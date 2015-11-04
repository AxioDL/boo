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

/** Supported buffer uses */
enum BufferUse
{
    BufferUseNull,
    BufferUseVertex,
    BufferUseIndex,
    BufferUseUniform
};

enum TextureType
{
    TextureStatic,
    Texture
};

struct ITexture
{
    enum Type
    {
        TextureStatic,
        TextureDynamic,
        TextureRender
    };
    Type type() const {return m_type;}
protected:
    Type m_type;
    ITexture(Type type) : m_type(type) {}
};

/** Static resource buffer for textures */
struct ITextureS : ITexture
{
protected:
    ITextureS() : ITexture(TextureStatic) {}
};

/** Dynamic resource buffer for textures */
struct ITextureD : ITexture
{
    virtual void load(const void* data, size_t sz)=0;
    virtual void* map(size_t sz)=0;
    virtual void unmap()=0;
protected:
    ITextureD() : ITexture(TextureDynamic) {}
};

/** Resource buffer for render-target textures */
struct ITextureR : ITexture
{
protected:
    ITextureR() : ITexture(TextureRender) {}
};

/** Supported texture formats */
enum TextureFormat
{
    TextureFormatRGBA8,
    TextureFormatDXT1,
    TextureFormatPVRTC4
};

/** Opaque token for representing the data layout of a vertex
 *  in a VBO. Also able to reference buffers for platforms like
 *  OpenGL that cache object refs */
struct IVertexFormat {};

/** Types of vertex attributes */
enum VertexSemantic
{
    VertexSemanticPosition,
    VertexSemanticNormal,
    VertexSemanticColor,
    VertexSemanticUV,
    VertexSemanticWeight
};

/** Used to create IVertexFormat */
struct VertexElementDescriptor
{
    const IGraphicsBuffer* vertBuffer = nullptr;
    const IGraphicsBuffer* indexBuffer = nullptr;
    VertexSemantic semantic;
    int semanticIdx = 0;
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
};

/** Used by platform shader pipeline constructors */
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

    virtual IGraphicsBufferS*
    newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count)=0;
    virtual IGraphicsBufferD*
    newDynamicBuffer(BufferUse use, size_t stride, size_t count)=0;

    virtual ITextureS*
    newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                     const void* data, size_t sz)=0;
    virtual ITextureD*
    newDynamicTexture(size_t width, size_t height, TextureFormat fmt)=0;
    virtual ITextureR*
    newRenderTexture(size_t width, size_t height, size_t samples)=0;

    virtual IVertexFormat*
    newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements)=0;

    virtual IShaderDataBinding*
    newShaderDataBinding(IShaderPipeline* pipeline,
                         IVertexFormat* vtxFormat,
                         IGraphicsBuffer* vbo, IGraphicsBuffer* ebo,
                         size_t ubufCount, IGraphicsBuffer** ubufs,
                         size_t texCount, ITexture** texs)=0;

    virtual void reset()=0;
    virtual IGraphicsData* commit()=0;
    virtual void destroyData(IGraphicsData*)=0;
    virtual void destroyAllData()=0;
};

}

#endif // IGFXDATAFACTORY_HPP
