#include "boo/IWindow.hpp"
#include "boo/IGraphicsContext.hpp"

#include <X11/Xlib.h>
#include <GL/glx.h>
#include <GL/glxext.h>

namespace boo
{

extern PFNGLXGETVIDEOSYNCSGIPROC FglXGetVideoSyncSGI;
extern PFNGLXWAITVIDEOSYNCSGIPROC FglXWaitVideoSyncSGI;
    
IGraphicsContext* _CGraphicsContextWaylandNew(IGraphicsContext::EGraphicsAPI api,
                                              IWindow* parentWindow);
    
struct WindowWayland : IWindow
{    
    WindowWayland(const std::string& title)
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

    void waitForRetrace()
    {
        unsigned int sync;
        FglXWaitVideoSyncSGI(1, 0, &sync);
    }

    uintptr_t getPlatformHandle() const
    {

    }
    
    ETouchType getTouchType() const
    {
        
    }
    
};

IWindow* _CWindowWaylandNew(const std::string& title)
{
    return new WindowWayland(title);
}
    
}
