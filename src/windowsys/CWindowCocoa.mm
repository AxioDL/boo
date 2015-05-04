#import <AppKit/AppKit.h>
#include "windowsys/IWindow.hpp"
#include "windowsys/IGraphicsContext.hpp"

@interface CWindowCocoaInternal : NSWindow
@end

namespace boo
{

class CWindowCocoa final : public IWindow
{
    
    CWindowCocoaInternal* m_nsWindow;
    IGraphicsContext* m_gfxCtx;
    
public:
    CWindowCocoa()
    {
        m_nsWindow = [CWindowCocoaInternal new];
        m_gfxCtx = IGraphicsContextNew(IGraphicsContext::API_OPENGL_3_3);
        m_gfxCtx->setPlatformWindowHandle(m_nsWindow);
        m_gfxCtx->initializeContext();
    }
    
    ~CWindowCocoa()
    {
        [m_nsWindow orderOut:nil];
        [m_nsWindow release];
        delete m_gfxCtx;
    }
    
    void showWindow()
    {
        [m_nsWindow makeKeyAndOrderFront:nil];
    }
    
    void hideWindow()
    {
        [m_nsWindow orderOut:nil];
    }
    
    std::string getTitle()
    {
        return [[m_nsWindow title] UTF8String];
    }
    
    void setTitle(const std::string& title)
    {
        [m_nsWindow setTitle:[NSString stringWithUTF8String:title.c_str()]];
    }
    
    void setWindowFrameDefault()
    {
        NSScreen* mainScreen = [NSScreen mainScreen];
        NSRect scrFrame = mainScreen.frame;
        float x_off = scrFrame.size.width / 3.0;
        float y_off = scrFrame.size.height / 3.0;
        [m_nsWindow setFrame:NSMakeRect(x_off, y_off, x_off * 2.0, y_off * 2.0) display:NO];
    }
    
    void getWindowFrame(float& xOut, float& yOut, float& wOut, float& hOut) const
    {
        NSRect wFrame = m_nsWindow.frame;
        xOut = wFrame.origin.x;
        yOut = wFrame.origin.y;
        wOut = wFrame.size.width;
        hOut = wFrame.size.height;
    }
    
    void setWindowFrame(float x, float y, float w, float h)
    {
        NSRect wFrame = NSMakeRect(x, y, w, h);
        [m_nsWindow setFrame:wFrame display:NO];
    }
    
    bool isFullscreen() const
    {
        return ([m_nsWindow styleMask] & NSFullScreenWindowMask) == NSFullScreenWindowMask;
    }
    
    void setFullscreen(bool fs)
    {
        if ((fs && !isFullscreen()) || (!fs && isFullscreen()))
            [m_nsWindow toggleFullScreen:nil];
    }
    
};
    
IWindow* IWindowNew()
{
    return new CWindowCocoa;
}
    
}

@implementation CWindowCocoaInternal
{
    
}
@end

