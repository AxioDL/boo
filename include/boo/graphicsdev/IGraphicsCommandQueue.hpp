#pragma once

#include "IGraphicsDataFactory.hpp"
#include "boo/IWindow.hpp"
#include <functional>

namespace boo
{

struct IGraphicsCommandQueue
{
    virtual ~IGraphicsCommandQueue() = default;

    using Platform = IGraphicsDataFactory::Platform;
    virtual Platform platform() const=0;
    virtual const SystemChar* platformName() const=0;

    virtual void setShaderDataBinding(const ObjToken<IShaderDataBinding>& binding)=0;
    virtual void setRenderTarget(const ObjToken<ITextureR>& target)=0;
    virtual void setViewport(const SWindowRect& rect, float znear=0.f, float zfar=1.f)=0;
    virtual void setScissor(const SWindowRect& rect)=0;

    virtual void resizeRenderTexture(const ObjToken<ITextureR>& tex, size_t width, size_t height)=0;
    virtual void schedulePostFrameHandler(std::function<void(void)>&& func)=0;

    virtual void setClearColor(const float rgba[4])=0;
    virtual void clearTarget(bool render=true, bool depth=true)=0;

    virtual void draw(size_t start, size_t count)=0;
    virtual void drawIndexed(size_t start, size_t count)=0;
    virtual void drawInstances(size_t start, size_t count, size_t instCount, size_t startInst=0)=0;
    virtual void drawInstancesIndexed(size_t start, size_t count, size_t instCount, size_t startInst=0)=0;

    virtual void resolveBindTexture(const ObjToken<ITextureR>& texture, const SWindowRect& rect,
                                    bool tlOrigin, int bindIdx, bool color, bool depth, bool clearDepth=false)=0;
    virtual void resolveDisplay(const ObjToken<ITextureR>& source)=0;
    virtual void execute()=0;

    virtual void startRenderer()=0;
    virtual void stopRenderer()=0;
};

}

