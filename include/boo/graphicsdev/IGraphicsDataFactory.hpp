#ifndef IGFXDATAFACTORY_HPP
#define IGFXDATAFACTORY_HPP

#include <memory>
#include <functional>
#include <stdint.h>
#include "boo/System.hpp"
#include "boo/ThreadLocalPtr.hpp"
#include "boo/BooObject.hpp"

namespace boo
{
struct IGraphicsCommandQueue;

/** Supported buffer uses */
enum class BufferUse
{
    Null,
    Vertex,
    Index,
    Uniform
};

/** Typeless graphics buffer */
struct IGraphicsBuffer : IObj
{
    bool dynamic() const { return m_dynamic; }
protected:
    bool m_dynamic;
    explicit IGraphicsBuffer(bool dynamic) : m_dynamic(dynamic) {}
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

/** Texture access types */
enum class TextureType
{
    Static,
    StaticArray,
    Dynamic,
    Render
};

/** Supported texture formats */
enum class TextureFormat
{
    RGBA8,
    I8,
    DXT1,
    PVRTC4
};

/** Supported texture clamp modes */
enum class TextureClampMode
{
    Repeat,
    ClampToWhite,
    ClampToEdge
};

/** Typeless texture */
struct ITexture : IObj
{
    TextureType type() const { return m_type; }

    /* Only applies on GL and Vulkan. Use shader semantics on other platforms */
    virtual void setClampMode(TextureClampMode mode) {}

protected:
    TextureType m_type;
    explicit ITexture(TextureType type) : m_type(type) {}
};

/** Static resource buffer for textures */
struct ITextureS : ITexture
{
protected:
    ITextureS() : ITexture(TextureType::Static) {}
};

/** Static-array resource buffer for array textures */
struct ITextureSA : ITexture
{
protected:
    ITextureSA() : ITexture(TextureType::StaticArray) {}
};

/** Dynamic resource buffer for textures */
struct ITextureD : ITexture
{
    virtual void load(const void* data, size_t sz)=0;
    virtual void* map(size_t sz)=0;
    virtual void unmap()=0;
protected:
    ITextureD() : ITexture(TextureType::Dynamic) {}
};

/** Resource buffer for render-target textures */
struct ITextureR : ITexture
{
protected:
    ITextureR() : ITexture(TextureType::Render) {}
};

/** Opaque token for representing the data layout of a vertex
 *  in a VBO. Also able to reference buffers for platforms like
 *  OpenGL that cache object refs */
struct IVertexFormat : IObj {};

/** Types of vertex attributes */
enum class VertexSemantic
{
    None = 0,
    Position3,
    Position4,
    Normal3,
    Normal4,
    Color,
    ColorUNorm,
    UV2,
    UV4,
    Weight,
    ModelView,
    SemanticMask = 0xf,
    Instanced = 0x10
};
ENABLE_BITWISE_ENUM(VertexSemantic)

/** Used to create IVertexFormat */
struct VertexElementDescriptor
{
    ObjToken<IGraphicsBuffer> vertBuffer;
    ObjToken<IGraphicsBuffer> indexBuffer;
    VertexSemantic semantic;
    int semanticIdx = 0;
    VertexElementDescriptor() = default;
    VertexElementDescriptor(const ObjToken<IGraphicsBuffer>& v, const ObjToken<IGraphicsBuffer>& i,
                            VertexSemantic s, int idx=0)
    : vertBuffer(v), indexBuffer(i), semantic(s), semanticIdx(idx) {}
};

/** Opaque token for referencing a complete graphics pipeline state necessary
 *  to rasterize geometry (shaders and blending modes mainly) */
struct IShaderPipeline : IObj {};

/** Opaque token serving as indirection table for shader resources
 *  and IShaderPipeline reference. Each renderable surface-material holds one
 *  as a reference */
struct IShaderDataBinding : IObj {};

/** Used wherever distinction of pipeline stages is needed */
enum class PipelineStage
{
    Vertex,
    Fragment
};

/** Used by platform shader pipeline constructors */
enum class Primitive
{
    Triangles,
    TriStrips
};

/** Used by platform shader pipeline constructors */
enum class CullMode
{
    None,
    Backface,
    Frontface
};

/** Used by platform shader pipeline constructors */
enum class ZTest
{
    None,
    LEqual, /* Flipped on Vulkan, D3D, Metal */
    Greater,
    GEqual,
    Equal
};

/** Used by platform shader pipeline constructors */
enum class BlendFactor
{
    Zero,
    One,
    SrcColor,
    InvSrcColor,
    DstColor,
    InvDstColor,
    SrcAlpha,
    InvSrcAlpha,
    DstAlpha,
    InvDstAlpha,
    SrcColor1,
    InvSrcColor1,

    /* Special value that activates DstColor - SrcColor blending */
    Subtract
};

/** Factory object for creating batches of resources as an IGraphicsData token */
struct IGraphicsDataFactory
{
    virtual ~IGraphicsDataFactory() = default;

    enum class Platform
    {
        Null,
        OpenGL,
        D3D11,
        D3D12,
        Metal,
        Vulkan,
        GX,
        GX2
    };
    virtual Platform platform() const=0;
    virtual const SystemChar* platformName() const=0;

    struct Context
    {
        virtual Platform platform() const=0;
        virtual const SystemChar* platformName() const=0;

        virtual ObjToken<IGraphicsBufferS>
        newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count)=0;
        virtual ObjToken<IGraphicsBufferD>
        newDynamicBuffer(BufferUse use, size_t stride, size_t count)=0;

        virtual ObjToken<ITextureS>
        newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                         TextureClampMode clampMode, const void* data, size_t sz)=0;
        virtual ObjToken<ITextureSA>
        newStaticArrayTexture(size_t width, size_t height, size_t layers, size_t mips,
                              TextureFormat fmt, TextureClampMode clampMode, const void* data, size_t sz)=0;
        virtual ObjToken<ITextureD>
        newDynamicTexture(size_t width, size_t height, TextureFormat fmt, TextureClampMode clampMode)=0;
        virtual ObjToken<ITextureR>
        newRenderTexture(size_t width, size_t height, TextureClampMode clampMode,
                         size_t colorBindingCount, size_t depthBindingCount)=0;

        virtual bool bindingNeedsVertexFormat() const=0;
        virtual ObjToken<IVertexFormat>
        newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements,
                        size_t baseVert = 0, size_t baseInst = 0)=0;

        virtual ObjToken<IShaderDataBinding>
        newShaderDataBinding(const ObjToken<IShaderPipeline>& pipeline,
                             const ObjToken<IVertexFormat>& vtxFormat,
                             const ObjToken<IGraphicsBuffer>& vbo,
                             const ObjToken<IGraphicsBuffer>& instVbo,
                             const ObjToken<IGraphicsBuffer>& ibo,
                             size_t ubufCount, const ObjToken<IGraphicsBuffer>* ubufs, const PipelineStage* ubufStages,
                             const size_t* ubufOffs, const size_t* ubufSizes,
                             size_t texCount, const ObjToken<ITexture>* texs,
                             const int* texBindIdx, const bool* depthBind,
                             size_t baseVert = 0, size_t baseInst = 0)=0;

        ObjToken<IShaderDataBinding>
        newShaderDataBinding(const ObjToken<IShaderPipeline>& pipeline,
                             const ObjToken<IVertexFormat>& vtxFormat,
                             const ObjToken<IGraphicsBuffer>& vbo,
                             const ObjToken<IGraphicsBuffer>& instVbo,
                             const ObjToken<IGraphicsBuffer>& ibo,
                             size_t ubufCount, const ObjToken<IGraphicsBuffer>* ubufs, const PipelineStage* ubufStages,
                             size_t texCount, const ObjToken<ITexture>* texs,
                             const int* texBindIdx, const bool* depthBind,
                             size_t baseVert = 0, size_t baseInst = 0)
        {
            return newShaderDataBinding(pipeline, vtxFormat, vbo, instVbo, ibo,
                                        ubufCount, ubufs, ubufStages, nullptr,
                                        nullptr, texCount, texs, texBindIdx, depthBind,
                                        baseVert, baseInst);
        }
    };

    virtual void commitTransaction(const std::function<bool(Context& ctx)>&)=0;

    virtual ObjToken<IGraphicsBufferD> newPoolBuffer(BufferUse use, size_t stride, size_t count)=0;
};

using GraphicsDataFactoryContext = IGraphicsDataFactory::Context;
using FactoryCommitFunc = std::function<bool(GraphicsDataFactoryContext& ctx)>;

}

#endif // IGFXDATAFACTORY_HPP
