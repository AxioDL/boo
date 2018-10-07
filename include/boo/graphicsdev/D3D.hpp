#ifndef GDEV_D3D_HPP
#define GDEV_D3D_HPP

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

class D3DDataFactory : public IGraphicsDataFactory
{
public:
    virtual ~D3DDataFactory() {}

    class Context final : public IGraphicsDataFactory::Context
    {
    public:
        bool bindingNeedsVertexFormat() const {return false;}
        virtual boo::ObjToken<IShaderPipeline>
        newShaderPipeline(const char* vertSource, const char* fragSource,
                          ComPtr<ID3DBlob>* vertBlobOut,
                          ComPtr<ID3DBlob>* fragBlobOut,
                          ComPtr<ID3DBlob>* pipelineBlob,
                          const boo::ObjToken<IVertexFormat>& vtxFmt,
                          BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                          ZTest depthTest, bool depthWrite, bool colorWrite,
                          bool alphaWrite, CullMode culling, bool overwriteAlpha=true)=0;
        virtual boo::ObjToken<IShaderPipeline>
        newTessellationShaderPipeline(
                          const char* vertSource, const char* fragSource,
                          const char* controlSource, const char* evaluationSource,
                          ComPtr<ID3DBlob>* vertBlobOut, ComPtr<ID3DBlob>* fragBlobOut,
                          ComPtr<ID3DBlob>* controlBlobOut, ComPtr<ID3DBlob>* evaluationBlobOut,
                          ComPtr<ID3DBlob>* pipelineBlob,
                          const boo::ObjToken<IVertexFormat>& vtxFmt,
                          BlendFactor srcFac, BlendFactor dstFac, uint32_t patchSize,
                          ZTest depthTest, bool depthWrite, bool colorWrite,
                          bool alphaWrite, CullMode culling, bool overwriteAlpha=true)=0;
    };
};

}

#endif // _WIN32
#endif // GDEV_D3D_HPP
