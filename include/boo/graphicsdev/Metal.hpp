#pragma once
#ifdef __APPLE__
#if BOO_HAS_METAL

#include "boo/BooObject.hpp"
#include "boo/IGraphicsContext.hpp"
#include "boo/System.hpp"
#include "boo/graphicsdev/IGraphicsCommandQueue.hpp"
#include "boo/graphicsdev/IGraphicsDataFactory.hpp"

namespace boo {
struct BaseGraphicsData;

class MetalDataFactory : public IGraphicsDataFactory {
public:
  class Context final : public IGraphicsDataFactory::Context {
    friend class MetalDataFactoryImpl;
    MetalDataFactory& m_parent;
    ObjToken<BaseGraphicsData> m_data;
    Context(MetalDataFactory& parent __BooTraceArgs);
    ~Context();

  public:
    Platform platform() const { return Platform::Metal; }
    const SystemChar* platformName() const { return _SYS_STR("Metal"); }

    ObjToken<IGraphicsBufferS> newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count);
    ObjToken<IGraphicsBufferD> newDynamicBuffer(BufferUse use, size_t stride, size_t count);

    ObjToken<ITextureS> newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                         TextureClampMode clampMode, const void* data, size_t sz);
    ObjToken<ITextureSA> newStaticArrayTexture(size_t width, size_t height, size_t layers, size_t mips,
                                               TextureFormat fmt, TextureClampMode clampMode, const void* data,
                                               size_t sz);
    ObjToken<ITextureD> newDynamicTexture(size_t width, size_t height, TextureFormat fmt, TextureClampMode clampMode);
    ObjToken<ITextureR> newRenderTexture(size_t width, size_t height, TextureClampMode clampMode, size_t colorBindCount,
                                         size_t depthBindCount);
    ObjToken<ITextureCubeR> newCubeRenderTexture(size_t width, size_t mips);

    ObjToken<IShaderStage> newShaderStage(const uint8_t* data, size_t size, PipelineStage stage);

    ObjToken<IShaderPipeline> newShaderPipeline(ObjToken<IShaderStage> vertex, ObjToken<IShaderStage> fragment,
                                                ObjToken<IShaderStage> geometry, ObjToken<IShaderStage> control,
                                                ObjToken<IShaderStage> evaluation, const VertexFormatInfo& vtxFmt,
                                                const AdditionalPipelineInfo& additionalInfo, bool asynchronous = true);

    ObjToken<IShaderDataBinding> newShaderDataBinding(
        const ObjToken<IShaderPipeline>& pipeline, const ObjToken<IGraphicsBuffer>& vbo,
        const ObjToken<IGraphicsBuffer>& instVbo, const ObjToken<IGraphicsBuffer>& ibo, size_t ubufCount,
        const ObjToken<IGraphicsBuffer>* ubufs, const PipelineStage* ubufStages, const size_t* ubufOffs,
        const size_t* ubufSizes, size_t texCount, const ObjToken<ITexture>* texs, const int* texBindIdxs,
        const bool* depthBind, size_t baseVert = 0, size_t baseInst = 0);
  };

  static std::vector<uint8_t> CompileMetal(const char* source, PipelineStage stage);
};

} // namespace boo

#endif
#endif // __APPLE__
