#ifndef IGFXCOMMANDQUEUE_HPP
#define IGFXCOMMANDQUEUE_HPP

#include "IGraphicsDataFactory.hpp"
#include "boo/IWindow.hpp"

namespace boo
{
enum class Primitive
{
    Triangles,
    TriStrips
};

struct IGraphicsCommandQueue
{
    virtual ~IGraphicsCommandQueue() {}

    using Platform = IGraphicsDataFactory::Platform;
    virtual Platform platform() const=0;
    virtual const SystemChar* platformName() const=0;

    virtual void setShaderDataBinding(IShaderDataBinding* binding)=0;
    virtual void setRenderTarget(ITextureR* target)=0;
    virtual void setViewport(const SWindowRect& rect)=0;
    virtual void setScissor(const SWindowRect& rect)=0;

    virtual void resizeRenderTexture(ITextureR* tex, size_t width, size_t height)=0;

    virtual void setClearColor(const float rgba[4])=0;
    virtual void clearTarget(bool render=true, bool depth=true)=0;

    virtual void setDrawPrimitive(Primitive prim)=0;
    virtual void draw(size_t start, size_t count)=0;
    virtual void drawIndexed(size_t start, size_t count)=0;
    virtual void drawInstances(size_t start, size_t count, size_t instCount)=0;
    virtual void drawInstancesIndexed(size_t start, size_t count, size_t instCount)=0;

    virtual void resolveDisplay(ITextureR* source)=0;
    virtual void execute()=0;

    virtual void stopRenderer()=0;
};

}

#endif // IGFXCOMMANDQUEUE_HPP
