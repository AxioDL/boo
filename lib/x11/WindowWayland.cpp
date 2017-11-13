#include "boo/IWindow.hpp"
#include "boo/IGraphicsContext.hpp"
#include "boo/audiodev/IAudioVoiceEngine.hpp"

#include <X11/Xlib.h>
#undef None

namespace boo
{
    
struct GraphicsContextWayland : IGraphicsContext
{

    EGraphicsAPI m_api;
    EPixelFormat m_pf;
    IWindow* m_parentWindow;

public:
    IWindowCallback* m_callback;

    GraphicsContextWayland(EGraphicsAPI api, IWindow* parentWindow)
    : m_api(api),
      m_pf(EPixelFormat::RGBA8),
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
        if (pf > EPixelFormat::RGBAF32_Z24)
            return;
        m_pf = pf;
    }

    bool initializeContext(void*)
    {
        return false;
    }

    void makeCurrent()
    {
    }

    void postInit()
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

    IGraphicsDataFactory* getMainContextDataFactory()
    {
        return nullptr;
    }

    IGraphicsDataFactory* getLoadContextDataFactory()
    {
        return nullptr;
    }

    void present()
    {
    }

};
    
struct WindowWayland : IWindow
{    
    GraphicsContextWayland m_gfxCtx;

    WindowWayland(std::string_view title)
        : m_gfxCtx(IGraphicsContext::EGraphicsAPI::OpenGL3_3, this)
    {
        
    }
    
    ~WindowWayland()
    {
        
    }
    
    void setCallback(IWindowCallback* cb)
    {
        
    }

    void closeWindow()
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
        return "";
    }
    
    void setTitle(const std::string& title)
    {
        
    }

    void setCursor(EMouseCursor cursor)
    {
    }

    void setWaitCursor(bool wait)
    {
    }
    
    void setWindowFrameDefault()
    {
        
    }
    
    void getWindowFrame(float& xOut, float& yOut, float& wOut, float& hOut) const
    {
        
    }
    
    void getWindowFrame(int& xOut, int& yOut, int& wOut, int& hOut) const
    {

    }

    void setWindowFrame(float x, float y, float w, float h)
    {
        
    }

    void setWindowFrame(int x, int y, int w, int h)
    {

    }
    
    float getVirtualPixelFactor() const
    {
        return 0;
    }
    
    void setStyle(EWindowStyle /*style*/)
    {}

    EWindowStyle getStyle() const
    {
        return EWindowStyle::None;
    }

    bool isFullscreen() const
    {
        return false;
    }
    
    void setFullscreen(bool fs)
    {
        
    }

    void claimKeyboardFocus(const int coord[2])
    {
    }

    bool clipboardCopy(EClipboardType type, const uint8_t* data, size_t sz)
    {
        return false;
    }

    std::unique_ptr<uint8_t[]> clipboardPaste(EClipboardType type, size_t& sz)
    {
        return std::unique_ptr<uint8_t[]>();
    }

    void waitForRetrace(IAudioVoiceEngine* engine)
    {
        if (engine)
            engine->pumpAndMixVoices();
    }

    uintptr_t getPlatformHandle() const
    {
        return 0;
    }
    
    ETouchType getTouchType() const
    {
        return ETouchType::None;
    }
    

    IGraphicsCommandQueue* getCommandQueue()
    {
        return m_gfxCtx.getCommandQueue();
    }

    IGraphicsDataFactory* getDataFactory()
    {
        return m_gfxCtx.getDataFactory();
    }

    IGraphicsDataFactory* getMainContextDataFactory()
    {
        return m_gfxCtx.getMainContextDataFactory();
    }

    IGraphicsDataFactory* getLoadContextDataFactory()
    {
        return m_gfxCtx.getLoadContextDataFactory();
    }

};

std::shared_ptr<IWindow> _WindowWaylandNew(const std::string& title)
{
    return std::make_shared<WindowWayland>(title);
}
    
}
