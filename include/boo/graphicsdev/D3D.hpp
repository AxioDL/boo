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

namespace boo
{

class ID3DDataFactory : public IGraphicsDataFactory
{
public:
    virtual IShaderPipeline* newShaderPipeline(const char* vertSource, const char* fragSource,
                                               ComPtr<ID3DBlob>& vertBlobOut, ComPtr<ID3DBlob>& fragBlobOut,
                                               IVertexFormat* vtxFmt,
                                               BlendFactor srcFac, BlendFactor dstFac,
                                               bool depthTest, bool depthWrite, bool backfaceCulling)=0;
};

}

#endif // _WIN32
#endif // GDEV_D3D_HPP
