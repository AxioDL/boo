#ifndef IGFXCONTEXT_HPP
#define IGFXCONTEXT_HPP

#include <memory>
#include <stdint.h>

namespace boo
{
struct IGraphicsCommandQueue;
struct IGraphicsDataFactory;

class IGraphicsContext
{
    friend class WindowCocoa;
    friend class WindowXCB;
    virtual void _setCallback(class IWindowCallback* cb) {(void)cb;}

public:
    
    enum class EGraphicsAPI
    {
        None       = 0,
        OpenGL3_3  = 1,
        OpenGL4_2  = 2,
        OpenGLES3  = 3,
        Vulkan     = 4,
        D3D11      = 5,
        D3D12      = 6,
        Metal      = 7,
        GX         = 8,
        GX2        = 9
    };
    
    enum class EPixelFormat
    {
        None        = 0,
        RGBA8       = 1, /* Default */
        RGBA8_Z24   = 2,
        RGBAF32     = 3,
        RGBAF32_Z24 = 4
    };
    
    virtual ~IGraphicsContext() {}
    
    virtual EGraphicsAPI getAPI() const=0;
    virtual EPixelFormat getPixelFormat() const=0;
    virtual void setPixelFormat(EPixelFormat pf)=0;
    virtual void initializeContext()=0;
    virtual void makeCurrent()=0;
    virtual void postInit()=0;
    virtual void present()=0;

    virtual IGraphicsCommandQueue* getCommandQueue()=0;
    virtual IGraphicsDataFactory* getDataFactory()=0;

    /* Creates a new context on current thread!! Call from main client thread */
    virtual IGraphicsDataFactory* getMainContextDataFactory()=0;

    /* Creates a new context on current thread!! Call from client loading thread */
    virtual IGraphicsDataFactory* getLoadContextDataFactory()=0;
};
    
}

#endif // IGFXCONTEXT_HPP
