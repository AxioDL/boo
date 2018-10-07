#ifndef IGFXCONTEXT_HPP
#define IGFXCONTEXT_HPP

#include <memory>
#include <cstdint>

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
        Vulkan     = 3,
        D3D11      = 4,
        Metal      = 6,
        GX         = 7,
        GX2        = 8,
        NX         = 9
    };
    
    enum class EPixelFormat
    {
        None        = 0,
        RGBA8       = 1, /* Default */
        RGBA16      = 2,
        RGBA8_Z24   = 3,
        RGBAF32     = 4,
        RGBAF32_Z24 = 5
    };
    
    virtual ~IGraphicsContext() = default;
    
    virtual EGraphicsAPI getAPI() const=0;
    virtual EPixelFormat getPixelFormat() const=0;
    virtual void setPixelFormat(EPixelFormat pf)=0;
    virtual bool initializeContext(void* handle)=0;
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
