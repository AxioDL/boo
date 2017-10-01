#ifndef GDEV_METAL_HPP
#define GDEV_METAL_HPP
#ifdef __APPLE__
#if BOO_HAS_METAL

#include "IGraphicsDataFactory.hpp"
#include "IGraphicsCommandQueue.hpp"
#include "boo/IGraphicsContext.hpp"

namespace boo
{

class MetalDataFactory : public IGraphicsDataFactory
{
public:
    class Context : public IGraphicsDataFactory::Context
    {
        friend class MetalDataFactoryImpl;
        MetalDataFactory& m_parent;
        Context(MetalDataFactory& parent) : m_parent(parent) {}
    public:
        Platform platform() const {return Platform::Metal;}
        const char* platformName() const {return "Metal";}

        IGraphicsBufferS* newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count);
        IGraphicsBufferD* newDynamicBuffer(BufferUse use, size_t stride, size_t count);

        ITextureS* newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                    TextureClampMode clampMode, const void* data, size_t sz);
        ITextureSA* newStaticArrayTexture(size_t width, size_t height, size_t layers, size_t mips,
                                          TextureFormat fmt, TextureClampMode clampMode, const void* data, size_t sz);
        ITextureD* newDynamicTexture(size_t width, size_t height, TextureFormat fmt, TextureClampMode clampMode);
        ITextureR* newRenderTexture(size_t width, size_t height, TextureClampMode clampMode,
                                    size_t colorBindCount, size_t depthBindCount);

        bool bindingNeedsVertexFormat() const {return false;}
        IVertexFormat* newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements,
                                       size_t baseVert = 0, size_t baseInst = 0);

        IShaderPipeline* newShaderPipeline(const char* vertSource, const char* fragSource,
                                           IVertexFormat* vtxFmt, unsigned targetSamples,
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
                             const int* texBindIdxs, const bool* depthBind,
                             size_t baseVert = 0, size_t baseInst = 0);
    };
};

}

#endif
#endif // __APPLE__
#endif // GDEV_METAL_HPP
