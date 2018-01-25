#ifndef GDEV_METAL_HPP
#define GDEV_METAL_HPP
#ifdef __APPLE__
#if BOO_HAS_METAL

#include "IGraphicsDataFactory.hpp"
#include "IGraphicsCommandQueue.hpp"
#include "boo/IGraphicsContext.hpp"

namespace boo
{
struct BaseGraphicsData;

class MetalDataFactory : public IGraphicsDataFactory
{
public:
    class Context : public IGraphicsDataFactory::Context
    {
        friend class MetalDataFactoryImpl;
        MetalDataFactory& m_parent;
        ObjToken<BaseGraphicsData> m_data;
        Context(MetalDataFactory& parent);
        ~Context();
    public:
        Platform platform() const { return Platform::Metal; }
        const SystemChar* platformName() const { return _S("Metal"); }

        ObjToken<IGraphicsBufferS> newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count);
        ObjToken<IGraphicsBufferD> newDynamicBuffer(BufferUse use, size_t stride, size_t count);

        ObjToken<ITextureS> newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                             TextureClampMode clampMode, const void* data, size_t sz);
        ObjToken<ITextureSA> newStaticArrayTexture(size_t width, size_t height, size_t layers, size_t mips,
                                                   TextureFormat fmt, TextureClampMode clampMode, const void* data,
                                                   size_t sz);
        ObjToken<ITextureD> newDynamicTexture(size_t width, size_t height, TextureFormat fmt,
                                              TextureClampMode clampMode);
        ObjToken<ITextureR> newRenderTexture(size_t width, size_t height, TextureClampMode clampMode,
                                             size_t colorBindCount, size_t depthBindCount);

        bool bindingNeedsVertexFormat() const { return false; }
        ObjToken<IVertexFormat> newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements,
                                                size_t baseVert = 0, size_t baseInst = 0);

        ObjToken<IShaderPipeline> newShaderPipeline(const char* vertSource, const char* fragSource,
                                                    std::vector<uint8_t>* vertBlobOut,
                                                    std::vector<uint8_t>* fragBlobOut,
                                                    const ObjToken<IVertexFormat>& vtxFmt,
                                                    BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                                                    ZTest depthTest, bool depthWrite, bool colorWrite,
                                                    bool alphaWrite, CullMode culling, bool depthAttachment = true);

        ObjToken<IShaderDataBinding>
        newShaderDataBinding(const ObjToken<IShaderPipeline>& pipeline,
                             const ObjToken<IVertexFormat>& vtxFormat,
                             const ObjToken<IGraphicsBuffer>& vbo,
                             const ObjToken<IGraphicsBuffer>& instVbo,
                             const ObjToken<IGraphicsBuffer>& ibo,
                             size_t ubufCount, const ObjToken<IGraphicsBuffer>* ubufs, const PipelineStage* ubufStages,
                             const size_t* ubufOffs, const size_t* ubufSizes,
                             size_t texCount, const ObjToken<ITexture>* texs,
                             const int* texBindIdxs, const bool* depthBind,
                             size_t baseVert = 0, size_t baseInst = 0);
    };
};

}

#endif
#endif // __APPLE__
#endif // GDEV_METAL_HPP
