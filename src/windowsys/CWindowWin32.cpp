#include "windowsys/IWindow.hpp"
#include "windowsys/IGraphicsContext.hpp"

namespace boo
{
    
IGraphicsContext* _CGraphicsContextWin32New(IGraphicsContext::EGraphicsAPI api,
                                            IWindow* parentWindow);
    
class CWindowWin32 final : public IWindow
{
    
    HWND m_hwnd;
    
public:
    
    CWindowWin32(const std::string& title)
    {
        m_hwnd = CreateWindowW(L"BooWindow", L"BooTest", WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               NULL, NULL, hInstance, NULL);
    }
    
    ~CWindowWin32()
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
    
    ETouchType getTouchType() const
    {
        
    }
    
};

IWindow* _CWindowWin32New(const std::string& title)
{
    return new CWindowWin32(title);
}
    
}
