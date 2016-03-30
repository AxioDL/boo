#ifndef GDEV_D3D_HPP
#define GDEV_D3D_HPP

#if _WIN32

#include "IGraphicsDataFactory.hpp"
#include "IGraphicsCommandQueue.hpp"
#include "boo/IGraphicsContext.hpp"
#include "boo/System.hpp"
#include <D3Dcommon.h>
#include <vector>
#include <unordered_set>

typedef HRESULT (WINAPI *pD3DCreateBlob)
    (SIZE_T     Size,
     ID3DBlob** ppBlob);
extern pD3DCreateBlob D3DCreateBlobPROC;

namespace boo
{

class ID3DDataFactory : public IGraphicsDataFactory
{
public:
    virtual ~ID3DDataFactory() {}

    class Context : public IGraphicsDataFactory::Context
    {
    public:
        bool bindingNeedsVertexFormat() const {return false;}
        virtual IShaderPipeline* newShaderPipeline(const char* vertSource, const char* fragSource,
                                                   ComPtr<ID3DBlob>& vertBlobOut, ComPtr<ID3DBlob>& fragBlobOut,
                                                   ComPtr<ID3DBlob>& pipelineBlob, IVertexFormat* vtxFmt,
                                                   BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                                                   bool depthTest, bool depthWrite, bool backfaceCulling)=0;
    };
};

}

#endif // _WIN32
#endif // GDEV_D3D_HPP
