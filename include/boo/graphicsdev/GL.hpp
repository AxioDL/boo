#ifndef GDEV_GL_HPP
#define GDEV_GL_HPP

#include "IGraphicsDataFactory.hpp"
#include "IGraphicsCommandQueue.hpp"
#include "boo/IGraphicsContext.hpp"
#include "GLSLMacros.hpp"

namespace boo
{

class GLDataFactory : public IGraphicsDataFactory
{
public:
    class Context : public IGraphicsDataFactory::Context
    {
        friend class GLDataFactoryImpl;
        GLDataFactory& m_parent;
        Context(GLDataFactory& parent) : m_parent(parent) {}
    public:
        Platform platform() const {return Platform::OpenGL;}
        const SystemChar* platformName() const {return _S("OpenGL");}

        IGraphicsBufferS* newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count);
        IGraphicsBufferD* newDynamicBuffer(BufferUse use, size_t stride, size_t count);

        ITextureS* newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                    const void* data, size_t sz);
        ITextureSA* newStaticArrayTexture(size_t width, size_t height, size_t layers, size_t mips,
                                          TextureFormat fmt, const void* data, size_t sz);
        ITextureD* newDynamicTexture(size_t width, size_t height, TextureFormat fmt);
        ITextureR* newRenderTexture(size_t width, size_t height,
                                    size_t colorBindingCount, size_t depthBindingCount);

        bool bindingNeedsVertexFormat() const {return true;}
        IVertexFormat* newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements,
                                       size_t baseVert = 0, size_t baseInst = 0);

        IShaderPipeline* newShaderPipeline(const char* vertSource, const char* fragSource,
                                           size_t texCount, const char** texNames,
                                           size_t uniformBlockCount, const char** uniformBlockNames,
                                           BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                                           ZTest depthTest, bool depthWrite, bool colorWrite,
                                           bool alphaWrite, CullMode culling);

        IShaderDataBinding*
        newShaderDataBinding(IShaderPipeline* pipeline,
                             IVertexFormat* vtxFormat,
                             IGraphicsBuffer* vbo, IGraphicsBuffer* instVbo, IGraphicsBuffer* ibo,
                             size_t ubufCount, IGraphicsBuffer** ubufs, const PipelineStage* ubufStages,
                             const size_t* ubufOffs, const size_t* ubufSizes,
                             size_t texCount, ITexture** texs,
                             const int* texBindIdx, const bool* depthBind,
                             size_t baseVert = 0, size_t baseInst = 0);
    };
};

}

#endif // GDEV_GL_HPP
