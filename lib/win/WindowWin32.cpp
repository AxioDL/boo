#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <Windows.h>
#include "boo/IWindow.hpp"
#include "boo/IGraphicsContext.hpp"

namespace boo
{
    
struct GraphicsContextWin32 : IGraphicsContext
{

    EGraphicsAPI m_api;
    EPixelFormat m_pf;
    IWindow* m_parentWindow;

public:
    IWindowCallback* m_callback;

    GraphicsContextWin32(EGraphicsAPI api, IWindow* parentWindow)
        : m_api(api),
        m_pf(PF_RGBA8),
        m_parentWindow(parentWindow)
    {}

    ~GraphicsContextWin32()
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

    IGraphicsContext* makeShareContext() const
    {

    }

    void makeCurrent()
    {

    }

    void clearCurrent()
    {

    }

    void swapBuffer()
    {

    }

};
    
class WindowWin32 : public IWindow
{
    
    HWND m_hwnd;
    
public:
    
    WindowWin32(const SystemString& title)
    {
        m_hwnd = CreateWindowW(L"BooWindow", title.c_str(), WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               NULL, NULL, NULL, NULL);
    }
    
    ~WindowWin32()
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
    
    SystemString getTitle()
    {
         wchar_t title[256];
         int c = GetWindowTextW(m_hwnd, title, 256);
         return SystemString(title, c);
    }
    
    void setTitle(const SystemString& title)
    {
        SetWindowTextW(m_hwnd, title.c_str());
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
        return 1.0;
    }
    
    bool isFullscreen() const
    {
        return false;
    }
    
    void setFullscreen(bool fs)
    {
        
    }

    void waitForRetrace()
    {
    }

    uintptr_t getPlatformHandle() const
    {
        return uintptr_t(m_hwnd);
    }
    
    ETouchType getTouchType() const
    {
        return TOUCH_NONE;
    }
    
};

IWindow* _WindowWin32New(const SystemString& title)
{
    return new WindowWin32(title);
}
    
}
