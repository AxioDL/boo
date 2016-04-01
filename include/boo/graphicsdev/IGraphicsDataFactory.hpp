#ifndef IGFXDATAFACTORY_HPP
#define IGFXDATAFACTORY_HPP

#include <memory>
#include <functional>
#include <stdint.h>
#include "boo/System.hpp"
#include "boo/ThreadLocalPtr.hpp"

namespace boo
{
struct IGraphicsCommandQueue;

struct IGraphicsBuffer
{
    bool dynamic() const {return m_dynamic;}
protected:
    bool m_dynamic;
    IGraphicsBuffer(bool dynamic) : m_dynamic(dynamic) {}
    virtual ~IGraphicsBuffer() {}
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
enum class BufferUse
{
    Null,
    Vertex,
    Index,
    Uniform
};

enum class TextureType
{
    Static,
    StaticArray,
    Dynamic,
    Render
};

struct ITexture
{
    TextureType type() const {return m_type;}
protected:
    TextureType m_type;
    ITexture(TextureType type) : m_type(type) {}
    virtual ~ITexture() {}
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

/** Supported texture formats */
enum class TextureFormat
{
    RGBA8,
    I8,
    DXT1,
    PVRTC4
};

/** Opaque token for representing the data layout of a vertex
 *  in a VBO. Also able to reference buffers for platforms like
 *  OpenGL that cache object refs */
struct IVertexFormat {};

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
    IGraphicsBuffer* vertBuffer = nullptr;
    IGraphicsBuffer* indexBuffer = nullptr;
    VertexSemantic semantic;
    int semanticIdx = 0;
    VertexElementDescriptor() = default;
    VertexElementDescriptor(IGraphicsBuffer* v, IGraphicsBuffer* i, VertexSemantic s, int idx=0)
    : vertBuffer(v), indexBuffer(i), semantic(s), semanticIdx(idx) {}
};

/** Opaque token for referencing a complete graphics pipeline state necessary
 *  to rasterize geometry (shaders and blending modes mainly) */
struct IShaderPipeline {};

/** Opaque token serving as indirection table for shader resources
 *  and IShaderPipeline reference. Each renderable surface-material holds one
 *  as a reference */
struct IShaderDataBinding {};

/** Opaque object for maintaining ownership of factory-created resources */
struct IGraphicsData {};
class GraphicsDataToken;

/** Used by platform shader pipeline constructors */
enum class Primitive
{
    Triangles,
    TriStrips
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
    InvSrcColor1
};

/** Factory object for creating batches of resources as an IGraphicsData token */
struct IGraphicsDataFactory
{
    virtual ~IGraphicsDataFactory() {}

    enum class Platform
    {
        Null,
        OGL,
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

        virtual IGraphicsBufferS*
        newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count)=0;
        virtual IGraphicsBufferD*
        newDynamicBuffer(BufferUse use, size_t stride, size_t count)=0;

        virtual ITextureS*
        newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                         const void* data, size_t sz)=0;
        virtual ITextureSA*
        newStaticArrayTexture(size_t width, size_t height, size_t layers, TextureFormat fmt,
                              const void* data, size_t sz)=0;
        virtual ITextureD*
        newDynamicTexture(size_t width, size_t height, TextureFormat fmt)=0;
        virtual ITextureR*
        newRenderTexture(size_t width, size_t height,
                         bool enableShaderColorBinding, bool enableShaderDepthBinding)=0;

        virtual bool bindingNeedsVertexFormat() const=0;
        virtual IVertexFormat*
        newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements)=0;

        virtual IShaderDataBinding*
        newShaderDataBinding(IShaderPipeline* pipeline,
                             IVertexFormat* vtxFormat,
                             IGraphicsBuffer* vbo, IGraphicsBuffer* instVbo, IGraphicsBuffer* ibo,
                             size_t ubufCount, IGraphicsBuffer** ubufs,
                             const size_t* ubufOffs, const size_t* ubufSizes,
                             size_t texCount, ITexture** texs)=0;

        IShaderDataBinding*
        newShaderDataBinding(IShaderPipeline* pipeline,
                             IVertexFormat* vtxFormat,
                             IGraphicsBuffer* vbo, IGraphicsBuffer* instVbo, IGraphicsBuffer* ibo,
                             size_t ubufCount, IGraphicsBuffer** ubufs,
                             size_t texCount, ITexture** texs)
        {
            return newShaderDataBinding(pipeline, vtxFormat, vbo, instVbo, ibo,
                                        ubufCount, ubufs, nullptr, nullptr, texCount, texs);
        }
    };

    virtual GraphicsDataToken commitTransaction(const std::function<bool(Context& ctx)>&)=0;

private:
    friend class GraphicsDataToken;
    virtual void destroyData(IGraphicsData*)=0;
    virtual void destroyAllData()=0;
};

using FactoryCommitFunc = std::function<bool(IGraphicsDataFactory::Context& ctx)>;

/** Ownership token for maintaining lifetime of factory-created resources
 *  deletion of this token triggers mass-deallocation of the factory's
 *  IGraphicsData (please don't delete and draw contained resources in the same frame). */
class GraphicsDataToken
{
    friend class GLDataFactory;
    friend class D3D12DataFactory;
    friend class D3D11DataFactory;
    friend class MetalDataFactory;
    friend class VulkanDataFactory;
    IGraphicsDataFactory* m_factory = nullptr;
    IGraphicsData* m_data = nullptr;
    GraphicsDataToken(IGraphicsDataFactory* factory, IGraphicsData* data)
    : m_factory(factory), m_data(data) {}
public:
    void doDestroy()
    {
        if (m_factory && m_data)
        {
            m_factory->destroyData(m_data);
            m_factory = nullptr;
            m_data = nullptr;
        }
    }
    GraphicsDataToken() = default;
    GraphicsDataToken(const GraphicsDataToken& other) = delete;
    GraphicsDataToken(GraphicsDataToken&& other)
    {
        m_factory = other.m_factory;
        other.m_factory = nullptr;
        m_data = other.m_data;
        other.m_data = nullptr;
    }
    GraphicsDataToken& operator=(const GraphicsDataToken& other) = delete;
    GraphicsDataToken& operator=(GraphicsDataToken&& other)
    {
        doDestroy();
        m_factory = other.m_factory;
        other.m_factory = nullptr;
        m_data = other.m_data;
        other.m_data = nullptr;
        return *this;
    }
    ~GraphicsDataToken() {doDestroy();}
    operator bool() const {return m_factory && m_data;}
};

}

#endif // IGFXDATAFACTORY_HPP
