#pragma once

#if _WIN32

#include "IGraphicsDataFactory.hpp"
#include "IGraphicsCommandQueue.hpp"
#include "boo/IGraphicsContext.hpp"
#include "boo/System.hpp"
#include <vector>
#include <unordered_set>

typedef HRESULT (WINAPI *pD3DCreateBlob)
    (SIZE_T     Size,
     ID3DBlob** ppBlob);
extern pD3DCreateBlob D3DCreateBlobPROC;

namespace boo
{
struct BaseGraphicsData;

class D3D11DataFactory : public IGraphicsDataFactory
{
public:
    virtual ~D3D11DataFactory() = default;

    Platform platform() const {return Platform::D3D11;}
    const SystemChar* platformName() const {return _SYS_STR("D3D11");}

    class Context final : public IGraphicsDataFactory::Context
    {
        friend class D3D11DataFactoryImpl;
        D3D11DataFactory& m_parent;
        boo::ObjToken<BaseGraphicsData> m_data;
        Context(D3D11DataFactory& parent __BooTraceArgs);
        ~Context();
    public:
        Platform platform() const {return Platform::D3D11;}
        const SystemChar* platformName() const {return _SYS_STR("D3D11");}

        boo::ObjToken<IGraphicsBufferS> newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count);
        boo::ObjToken<IGraphicsBufferD> newDynamicBuffer(BufferUse use, size_t stride, size_t count);

        boo::ObjToken<ITextureS> newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                                  TextureClampMode clampMode, const void* data, size_t sz);
        boo::ObjToken<ITextureSA> newStaticArrayTexture(size_t width, size_t height, size_t layers, size_t mips,
                                                        TextureFormat fmt, TextureClampMode clampMode,
                                                        const void* data, size_t sz);
        boo::ObjToken<ITextureD> newDynamicTexture(size_t width, size_t height, TextureFormat fmt, TextureClampMode clampMode);
        boo::ObjToken<ITextureR> newRenderTexture(size_t width, size_t height, TextureClampMode clampMode,
                                                  size_t colorBindCount, size_t depthBindCount);

        ObjToken<IShaderStage>
        newShaderStage(const uint8_t* data, size_t size, PipelineStage stage);

        ObjToken<IShaderPipeline>
        newShaderPipeline(ObjToken<IShaderStage> vertex, ObjToken<IShaderStage> fragment,
                          ObjToken<IShaderStage> geometry, ObjToken<IShaderStage> control,
                          ObjToken<IShaderStage> evaluation, const VertexFormatInfo& vtxFmt,
                          const AdditionalPipelineInfo& additionalInfo);

        boo::ObjToken<IShaderDataBinding>
        newShaderDataBinding(const boo::ObjToken<IShaderPipeline>& pipeline,
                             const boo::ObjToken<IGraphicsBuffer>& vbo,
                             const boo::ObjToken<IGraphicsBuffer>& instVbo,
                             const boo::ObjToken<IGraphicsBuffer>& ibo,
                             size_t ubufCount, const boo::ObjToken<IGraphicsBuffer>* ubufs, const PipelineStage* ubufStages,
                             const size_t* ubufOffs, const size_t* ubufSizes,
                             size_t texCount, const boo::ObjToken<ITexture>* texs,
                             const int* bindIdxs, const bool* bindDepth,
                             size_t baseVert = 0, size_t baseInst = 0);
    };

    static std::vector<uint8_t> CompileHLSL(const char* source, PipelineStage stage);
};

}

#endif // _WIN32
