#include "windowsys/IGraphicsContext.hpp"
#include "windowsys/IWindow.hpp"

namespace boo
{

class CGraphicsContextXCB final : public IGraphicsContext
{
    
    EGraphicsAPI m_api;
    EPixelFormat m_pf;
    IWindow* m_parentWindow;
    
public:
    IWindowCallback* m_callback;
    
    CGraphicsContextXCB(EGraphicsAPI api, IWindow* parentWindow)
    : m_api(api),
      m_pf(PF_RGBA8),
      m_parentWindow(parentWindow)
    {}
    
    ~CGraphicsContextXCB()
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
    
    void setPlatformWindowHandle(void* handle)
    {
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

IGraphicsContext* _CGraphicsContextXCBNew(IGraphicsContext::EGraphicsAPI api,
                                          IWindow* parentWindow)
{
    
}
    
}
