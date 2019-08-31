#pragma once

#if _WIN32

#include <cstddef>
#include <vector>

#include "boo/BooObject.hpp"
#include "boo/System.hpp"
#include "boo/graphicsdev/IGraphicsDataFactory.hpp"

using pD3DCreateBlob = HRESULT(WINAPI*)(SIZE_T Size, ID3DBlob** ppBlob);
extern pD3DCreateBlob D3DCreateBlobPROC;

namespace boo {
struct BaseGraphicsData;

class D3D11DataFactory : public IGraphicsDataFactory {
public:
  ~D3D11DataFactory() override = default;

  Platform platform() const override { return Platform::D3D11; }
  const SystemChar* platformName() const override { return _SYS_STR("D3D11"); }

  class Context final : public IGraphicsDataFactory::Context {
    friend class D3D11DataFactoryImpl;
    D3D11DataFactory& m_parent;
    ObjToken<BaseGraphicsData> m_data;
    Context(D3D11DataFactory& parent __BooTraceArgs);
    ~Context();

  public:
    Platform platform() const override { return Platform::D3D11; }
    const SystemChar* platformName() const override { return _SYS_STR("D3D11"); }

    ObjToken<IGraphicsBufferS> newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count) override;
    ObjToken<IGraphicsBufferD> newDynamicBuffer(BufferUse use, size_t stride, size_t count) override;

    ObjToken<ITextureS> newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                         TextureClampMode clampMode, const void* data, size_t sz) override;
    ObjToken<ITextureSA> newStaticArrayTexture(size_t width, size_t height, size_t layers, size_t mips,
                                               TextureFormat fmt, TextureClampMode clampMode, const void* data,
                                               size_t sz) override;
    ObjToken<ITextureD> newDynamicTexture(size_t width, size_t height, TextureFormat fmt,
                                          TextureClampMode clampMode) override;
    ObjToken<ITextureR> newRenderTexture(size_t width, size_t height, TextureClampMode clampMode, size_t colorBindCount,
                                         size_t depthBindCount) override;
    ObjToken<ITextureCubeR> newCubeRenderTexture(size_t width, size_t mips) override;

    ObjToken<IShaderStage> newShaderStage(const uint8_t* data, size_t size, PipelineStage stage) override;

    ObjToken<IShaderPipeline> newShaderPipeline(ObjToken<IShaderStage> vertex, ObjToken<IShaderStage> fragment,
                                                ObjToken<IShaderStage> geometry, ObjToken<IShaderStage> control,
                                                ObjToken<IShaderStage> evaluation, const VertexFormatInfo& vtxFmt,
                                                const AdditionalPipelineInfo& additionalInfo,
                                                bool asynchronous = true) override;

    ObjToken<IShaderDataBinding>
    newShaderDataBinding(const ObjToken<IShaderPipeline>& pipeline, const ObjToken<IGraphicsBuffer>& vbo,
                         const ObjToken<IGraphicsBuffer>& instVbo, const ObjToken<IGraphicsBuffer>& ibo,
                         size_t ubufCount, const ObjToken<IGraphicsBuffer>* ubufs, const PipelineStage* ubufStages,
                         const size_t* ubufOffs, const size_t* ubufSizes, size_t texCount,
                         const ObjToken<ITexture>* texs, const int* bindIdxs, const bool* bindDepth,
                         size_t baseVert = 0, size_t baseInst = 0) override;
  };

  static std::vector<uint8_t> CompileHLSL(const char* source, PipelineStage stage);
};

} // namespace boo

#endif // _WIN32
