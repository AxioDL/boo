#import <AppKit/AppKit.h>
#include "IApplication.hpp"
#include "windowsys/IWindow.hpp"
#include "windowsys/IGraphicsContext.hpp"

namespace boo {class CWindowCocoa;}
@interface CWindowCocoaInternal : NSWindow
{
    boo::CWindowCocoa* booWindow;
}
- (id)initWithBooWindow:(boo::CWindowCocoa*)bw title:(const std::string&)title;
- (void)setFrameDefault;
- (NSRect)genFrameDefault;
@end

namespace boo
{
    
IGraphicsContext* _CGraphicsContextCocoaNew(IGraphicsContext::EGraphicsAPI api,
                                            IWindow* parentWindow);

class CWindowCocoa final : public IWindow
{
    
    CWindowCocoaInternal* m_nsWindow;
    IGraphicsContext* m_gfxCtx;

public:

    CWindowCocoa(const std::string& title)
    {
        m_nsWindow = [[CWindowCocoaInternal alloc] initWithBooWindow:this title:title];
        m_gfxCtx = _CGraphicsContextCocoaNew(IGraphicsContext::API_OPENGL_3_3, this);
        m_gfxCtx->initializeContext();
    }
    
    void _clearWindow()
    {
        m_nsWindow = NULL;
    }
    
    ~CWindowCocoa()
    {
        [m_nsWindow orderOut:nil];
        [m_nsWindow release];
        delete m_gfxCtx;
        IApplicationInstance()->_deletedWindow(this);
    }
    
    void setCallback(IWindowCallback* cb)
    {
        m_gfxCtx->_setCallback(cb);
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
        [m_nsWindow setTitle:[[NSString stringWithUTF8String:title.c_str()] autorelease]];
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
    
    float getVirtualPixelFactor() const
    {
        return [m_nsWindow backingScaleFactor];
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
    
    ETouchType getTouchType() const
    {
        return TOUCH_TRACKPAD;
    }
    
    uintptr_t getPlatformHandle() const
    {
        return (uintptr_t)m_nsWindow;
    }
    
};
    
IWindow* _CWindowCocoaNew(const std::string& title)
{
    return new CWindowCocoa(title);
}
    
}

@implementation CWindowCocoaInternal
- (id)initWithBooWindow:(boo::CWindowCocoa *)bw title:(const std::string&)title
{
    self = [self initWithContentRect:[self genFrameDefault]
                           styleMask:NSTitledWindowMask|
                                     NSClosableWindowMask|
                                     NSMiniaturizableWindowMask|
                                     NSResizableWindowMask
                             backing:NSBackingStoreBuffered
                               defer:YES];
    self.title = [[NSString stringWithUTF8String:title.c_str()] autorelease];
    booWindow = bw;
    return self;
}
- (void)setFrameDefault
{
    [self setFrame:[self genFrameDefault] display:NO];
}
- (NSRect)genFrameDefault
{
    NSScreen* mainScreen = [NSScreen mainScreen];
    NSRect scrFrame = mainScreen.frame;
    float width = scrFrame.size.width * 2.0 / 3.0;
    float height = scrFrame.size.height * 2.0 / 3.0;
    return NSMakeRect((scrFrame.size.width - width) / 2.0,
                      (scrFrame.size.height - height) / 2.0,
                      width, height);
}
- (void)close
{
    booWindow->_clearWindow();
    [super close];
}
- (BOOL)acceptsFirstResponder
{
    return YES;
}
- (BOOL)acceptsMouseMovedEvents
{
    return YES;
}
- (NSWindowCollectionBehavior)collectionBehavior
{
    return NSWindowCollectionBehaviorFullScreenPrimary;
}
@end

