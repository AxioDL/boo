#include "windowsys/IWindow.hpp"
#include "windowsys/IGraphicsContext.hpp"

namespace boo
{
    
IGraphicsContext* _CGraphicsContextXCBNew(IGraphicsContext::EGraphicsAPI api,
                                          IWindow* parentWindow);

class CWindowXCB final : public IWindow
{
    
    
public:
    
    CWindowXCB(const std::string& title)
    {
        
    }
    
    ~CWindowXCB()
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

IWindow* _CWindowXCBNew(const std::string& title)
{
    return new CWindowXCB(title);
}
    
}
