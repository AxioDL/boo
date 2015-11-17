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
    
    enum EGraphicsAPI
    {
        API_NONE       = 0,
        API_OPENGL_3_3 = 1,
        API_OPENGL_4_2 = 2,
        API_OPENGLES_3 = 3,
        API_VULKAN     = 4,
        API_D3D11      = 5,
        API_D3D12      = 6,
        API_METAL      = 7,
        API_GX         = 8,
        API_GX2        = 9
    };
    
    enum EPixelFormat
    {
        PF_NONE        = 0,
        PF_RGBA8       = 1, /* Default */
        PF_RGBA8_Z24   = 2,
        PF_RGBAF32     = 3,
        PF_RGBAF32_Z24 = 4
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
