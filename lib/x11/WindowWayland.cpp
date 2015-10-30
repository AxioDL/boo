#include "boo/IWindow.hpp"
#include "boo/IGraphicsContext.hpp"

#include <X11/Xlib.h>
#include <GL/glx.h>
#include <GL/glxext.h>

namespace boo
{

extern PFNGLXGETVIDEOSYNCSGIPROC FglXGetVideoSyncSGI;
extern PFNGLXWAITVIDEOSYNCSGIPROC FglXWaitVideoSyncSGI;
    
struct GraphicsContextWayland : IGraphicsContext
{

    EGraphicsAPI m_api;
    EPixelFormat m_pf;
    IWindow* m_parentWindow;

public:
    IWindowCallback* m_callback;

    GraphicsContextWayland(EGraphicsAPI api, IWindow* parentWindow)
    : m_api(api),
      m_pf(PF_RGBA8),
      m_parentWindow(parentWindow)
    {}

    ~GraphicsContextWayland()
    {

    }

    void _setCallback(IWindowCallback* cb)
    {
        m_callback = cb;
    }

    EGraphicsAPI getAPI() const
    {
        return m_api;
    }

    EPixelFormat getPixelFormat() const
    {
        return m_pf;
    }

    void setPixelFormat(EPixelFormat pf)
    {
        if (pf > PF_RGBAF32_Z24)
            return;
        m_pf = pf;
    }

    void initializeContext()
    {

    }

    void makeCurrent()
    {
    }

    IGraphicsCommandQueue* getCommandQueue()
    {
        return nullptr;
    }

    IGraphicsDataFactory* getDataFactory()
    {
        return nullptr;
    }

    IGraphicsDataFactory* getLoadContextDataFactory()
    {
        return nullptr;
    }

};
    
struct WindowWayland : IWindow
{    
    GraphicsContextWayland m_gfxCtx;

    WindowWayland(const std::string& title)
    : m_gfxCtx(IGraphicsContext::API_OPENGL_3_3, this)
    {
        
    }
    
    ~WindowWayland()
    {
        
    }
    
    void setCallback(IWindowCallback* cb)
    {
        
    }
    
    void showWindow()
    {
        
    }
    
    void hideWindow()
    {
        
    }
    
    std::string getTitle()
    {
        
    }
    
    void setTitle(const std::string& title)
    {
        
    }
    
    void setWindowFrameDefault()
    {
        
    }
    
    void getWindowFrame(float& xOut, float& yOut, float& wOut, float& hOut) const
    {
        
    }
    
    void setWindowFrame(float x, float y, float w, float h)
    {
        
    }
    
    float getVirtualPixelFactor() const
    {
        
    }
    
    bool isFullscreen() const
    {
        
    }
    
    void setFullscreen(bool fs)
    {
        
    }

    size_t waitForRetrace(size_t count)
    {
        unsigned int sync;
        FglXWaitVideoSyncSGI(1, 0, &sync);
        return 0;
    }

    uintptr_t getPlatformHandle() const
    {

    }
    
    ETouchType getTouchType() const
    {
        
    }
    

    IGraphicsCommandQueue* getCommandQueue()
    {
        return m_gfxCtx.getCommandQueue();
    }

    IGraphicsDataFactory* getDataFactory()
    {
        return m_gfxCtx.getDataFactory();
    }

    IGraphicsDataFactory* getLoadContextDataFactory()
    {
        return m_gfxCtx.getLoadContextDataFactory();
    }

};

IWindow* _WindowWaylandNew(const std::string& title)
{
    return new WindowWayland(title);
}
    
}
