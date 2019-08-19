#pragma once
#if BOO_HAS_NX

#include <unordered_map>

#include "boo/BooObject.hpp"
#include "boo/graphicsdev/IGraphicsCommandQueue.hpp"
#include "boo/graphicsdev/IGraphicsDataFactory.hpp"
#include "boo/graphicsdev/nx_compiler.hpp"

#include <switch/nvidia/fence.h>

struct pipe_screen;
struct pipe_context;
struct st_context;
struct pipe_surface;

namespace boo {
struct BaseGraphicsData;

struct NXContext {
  struct pipe_surface* m_windowSurfaces[2];
  NvFence m_fences[2];
  bool m_fence_swap;

  bool initialize();
  bool terminate();
  bool _resizeWindowSurfaces();

  unsigned m_sampleCount = 1;

  struct pipe_screen* m_screen = nullptr;
  struct pipe_context* m_pctx = nullptr;
  struct st_context* m_st = nullptr;
  nx_compiler m_compiler;

  std::unordered_map<uint32_t, void*> m_samplers;
  std::unordered_map<uint32_t, void*> m_blendStates;
  std::unordered_map<uint32_t, void*> m_rasStates;
  std::unordered_map<uint32_t, void*> m_dsStates;
  std::unordered_map<uint64_t, void*> m_vtxElemStates;
};

class NXDataFactory : public IGraphicsDataFactory {
public:
  class Context final : public IGraphicsDataFactory::Context {
    friend class NXDataFactoryImpl;
    NXDataFactory& m_parent;
    boo::ObjToken<BaseGraphicsData> m_data;
    Context(NXDataFactory& parent __BooTraceArgs);
    ~Context();

  public:
    Platform platform() const { return Platform::NX; }
    const SystemChar* platformName() const { return _SYS_STR("NX"); }

    boo::ObjToken<IGraphicsBufferS> newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count);
    boo::ObjToken<IGraphicsBufferD> newDynamicBuffer(BufferUse use, size_t stride, size_t count);

    boo::ObjToken<ITextureS> newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                              TextureClampMode clampMode, const void* data, size_t sz);
    boo::ObjToken<ITextureSA> newStaticArrayTexture(size_t width, size_t height, size_t layers, size_t mips,
                                                    TextureFormat fmt, TextureClampMode clampMode, const void* data,
                                                    size_t sz);
    boo::ObjToken<ITextureD> newDynamicTexture(size_t width, size_t height, TextureFormat fmt,
                                               TextureClampMode clampMode);
    boo::ObjToken<ITextureR> newRenderTexture(size_t width, size_t height, TextureClampMode clampMode,
                                              size_t colorBindCount, size_t depthBindCount);

    ObjToken<IShaderStage> newShaderStage(const uint8_t* data, size_t size, PipelineStage stage);

    ObjToken<IShaderPipeline> newShaderPipeline(ObjToken<IShaderStage> vertex, ObjToken<IShaderStage> fragment,
                                                ObjToken<IShaderStage> geometry, ObjToken<IShaderStage> control,
                                                ObjToken<IShaderStage> evaluation, const VertexFormatInfo& vtxFmt,
                                                const AdditionalPipelineInfo& additionalInfo);

    boo::ObjToken<IShaderDataBinding> newShaderDataBinding(
        const boo::ObjToken<IShaderPipeline>& pipeline, const boo::ObjToken<IGraphicsBuffer>& vbo,
        const boo::ObjToken<IGraphicsBuffer>& instVbo, const boo::ObjToken<IGraphicsBuffer>& ibo, size_t ubufCount,
        const boo::ObjToken<IGraphicsBuffer>* ubufs, const PipelineStage* ubufStages, const size_t* ubufOffs,
        const size_t* ubufSizes, size_t texCount, const boo::ObjToken<ITexture>* texs, const int* bindIdxs,
        const bool* bindDepth, size_t baseVert = 0, size_t baseInst = 0);
  };
};

} // namespace boo

#endif
