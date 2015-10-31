#ifndef IGFXCOMMANDQUEUE_HPP
#define IGFXCOMMANDQUEUE_HPP

#include "IGraphicsDataFactory.hpp"

namespace boo
{
enum Primitive
{
    PrimitiveTriangles,
    PrimitiveTriStrips
};

struct IGraphicsCommandQueue
{
    virtual ~IGraphicsCommandQueue() {}

    using Platform = IGraphicsDataFactory::Platform;
    virtual Platform platform() const=0;
    virtual const char* platformName() const=0;

    virtual void setShaderDataBinding(const IShaderDataBinding* binding)=0;
    virtual void setRenderTarget(const ITextureD* target)=0;

    virtual void setClearColor(const float rgba[4])=0;
    virtual void clearTarget(bool render=true, bool depth=true)=0;

    virtual void setDrawPrimitive(Primitive prim)=0;
    virtual void draw(size_t start, size_t count)=0;
    virtual void drawIndexed(size_t start, size_t count)=0;
    virtual void drawInstances(size_t start, size_t count, size_t instCount)=0;
    virtual void drawInstancesIndexed(size_t start, size_t count, size_t instCount)=0;

    virtual void present()=0;
    virtual void execute()=0;
};

}

#endif // IGFXCOMMANDQUEUE_HPP
