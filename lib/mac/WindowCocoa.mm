#include "boo/graphicsdev/GL.hpp"
#include "boo/graphicsdev/glew.h"
#include "boo/graphicsdev/Metal.hpp"
#include "CocoaCommon.hpp"
#import <AppKit/AppKit.h>
#import <CoreVideo/CVDisplayLink.h>
#include "boo/IApplication.hpp"
#include "boo/IWindow.hpp"
#include "boo/IGraphicsContext.hpp"

#include <LogVisor/LogVisor.hpp>

namespace boo {class WindowCocoa;}
@interface WindowCocoaInternal : NSWindow
{
    boo::WindowCocoa* booWindow;
}
- (id)initWithBooWindow:(boo::WindowCocoa*)bw title:(const std::string&)title;
- (void)setFrameDefault;
- (NSRect)genFrameDefault;
@end
    
/* AppKit applies OpenGL much differently than other platforms
 * the NSOpenGLView class composes together all necessary
 * OGL context members and provides the necessary event hooks
 * for KB/Mouse/Touch events
 */

static const NSOpenGLPixelFormatAttribute PF_RGBA8_ATTRS[] =
{
    NSOpenGLPFAAccelerated,
    NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFAColorSize, 24,
    NSOpenGLPFAAlphaSize, 8,
};

static const NSOpenGLPixelFormatAttribute PF_RGBA8_Z24_ATTRS[] =
{
    NSOpenGLPFAAccelerated,
    NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFAColorSize, 24,
    NSOpenGLPFAAlphaSize, 8,
    NSOpenGLPFADepthSize, 24,
};

static const NSOpenGLPixelFormatAttribute PF_RGBAF32_ATTRS[] =
{
    NSOpenGLPFAAccelerated,
    NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFAColorFloat,
    NSOpenGLPFAColorSize, 96,
    NSOpenGLPFAAlphaSize, 32,
};

static const NSOpenGLPixelFormatAttribute PF_RGBAF32_Z24_ATTRS[] =
{
    NSOpenGLPFAAccelerated,
    NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFAColorFloat,
    NSOpenGLPFAColorSize, 96,
    NSOpenGLPFAAlphaSize, 32,
    NSOpenGLPFADepthSize, 24,
};

static const NSOpenGLPixelFormatAttribute* PF_TABLE[] =
{
    NULL,
    PF_RGBA8_ATTRS,
    PF_RGBA8_Z24_ATTRS,
    PF_RGBAF32_ATTRS,
    PF_RGBAF32_Z24_ATTRS
};

namespace boo
{
class GraphicsContextCocoa : public IGraphicsContext
{
protected:
    EGraphicsAPI m_api;
    EPixelFormat m_pf;
    IWindow* m_parentWindow;
    CVDisplayLinkRef m_dispLink = nullptr;
    
    GraphicsContextCocoa(EGraphicsAPI api, EPixelFormat pf, IWindow* parentWindow)
    : m_api(api), m_pf(pf), m_parentWindow(parentWindow) {}
    
    std::mutex m_dlmt;
    std::condition_variable m_dlcv;
    
    static CVReturn DLCallback(CVDisplayLinkRef CV_NONNULL displayLink,
                               const CVTimeStamp * CV_NONNULL inNow,
                               const CVTimeStamp * CV_NONNULL inOutputTime,
                               CVOptionFlags flagsIn,
                               CVOptionFlags * CV_NONNULL flagsOut,
                               GraphicsContextCocoa* CV_NULLABLE ctx)
    {
        ctx->m_dlcv.notify_one();
        return kCVReturnSuccess;
    }
    
    ~GraphicsContextCocoa()
    {
        if (m_dispLink)
        {
            CVDisplayLinkStop(m_dispLink);
            CVDisplayLinkRelease(m_dispLink);
        }
    }
    
public:
    IWindowCallback* m_callback = nullptr;
    void waitForRetrace()
    {
        std::unique_lock<std::mutex> lk(m_dlmt);
        m_dlcv.wait(lk);
    }
};
class GraphicsContextCocoaGL;
class GraphicsContextCocoaMetal;
}

@interface BooCocoaResponder : NSResponder
{
    @public
    NSUInteger lastModifiers;
    boo::GraphicsContextCocoa* booContext;
    NSView* parentView;
}
- (id)initWithBooContext:(boo::GraphicsContextCocoa*)bctx View:(NSView*)view;
@end

@interface GraphicsContextCocoaGLInternal : NSOpenGLView
{
    BooCocoaResponder* resp;
}
- (id)initWithBooContext:(boo::GraphicsContextCocoaGL*)bctx;
@end

@interface GraphicsContextCocoaMetalInternal : NSView
{
    BooCocoaResponder* resp;
    boo::MetalContext* m_ctx;
}
- (id)initWithBooContext:(boo::GraphicsContextCocoaMetal*)bctx;
@end
    
namespace boo
{
static LogVisor::LogModule Log("boo::WindowCocoa");
IGraphicsCommandQueue* _NewGLCommandQueue(IGraphicsContext* parent);
IGraphicsCommandQueue* _NewMetalCommandQueue(MetalContext* ctx, IWindow* parentWindow,
                                             IGraphicsContext* parent);
void _CocoaUpdateLastGLCtx(NSOpenGLContext* lastGLCtx);

class GraphicsContextCocoaGL : public GraphicsContextCocoa
{
    GraphicsContextCocoaGLInternal* m_nsContext = nullptr;
    
    IGraphicsCommandQueue* m_commandQueue = nullptr;
    IGraphicsDataFactory* m_dataFactory = nullptr;
    NSOpenGLContext* m_loadCtx = nullptr;
    
public:
    NSOpenGLContext* m_lastCtx = nullptr;
    
    GraphicsContextCocoaGL(EGraphicsAPI api, IWindow* parentWindow, NSOpenGLContext* lastGLCtx)
    : GraphicsContextCocoa(api, PF_RGBA8, parentWindow),
      m_lastCtx(lastGLCtx)
    {
        m_dataFactory = new GLDataFactory(this);
    }
    
    ~GraphicsContextCocoaGL()
    {
        delete m_dataFactory;
        delete m_commandQueue;
        [m_nsContext release];
        [m_loadCtx release];
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
        m_nsContext = [[GraphicsContextCocoaGLInternal alloc] initWithBooContext:this];
        if (!m_nsContext)
            Log.report(LogVisor::FatalError, "unable to make new NSOpenGLView");
        [(NSWindow*)m_parentWindow->getPlatformHandle() setContentView:m_nsContext];
        CVDisplayLinkCreateWithActiveCGDisplays(&m_dispLink);
        CVDisplayLinkSetOutputCallback(m_dispLink, (CVDisplayLinkOutputCallback)DLCallback, this);
        CVDisplayLinkStart(m_dispLink);
        m_commandQueue = _NewGLCommandQueue(this);
    }
    
    void makeCurrent()
    {
        [[m_nsContext openGLContext] makeCurrentContext];
    }
    
    void postInit()
    {
    }
    
    IGraphicsCommandQueue* getCommandQueue()
    {
        return m_commandQueue;
    }
    
    IGraphicsDataFactory* getDataFactory()
    {
        return m_dataFactory;
    }
    
    IGraphicsDataFactory* getLoadContextDataFactory()
    {
        if (!m_loadCtx)
        {
            NSOpenGLPixelFormat* nspf = [[NSOpenGLPixelFormat alloc] initWithAttributes:PF_TABLE[m_pf]];
            m_loadCtx = [[NSOpenGLContext alloc] initWithFormat:nspf shareContext:[m_nsContext openGLContext]];
            [nspf release];
            if (!m_loadCtx)
                Log.report(LogVisor::FatalError, "unable to make load NSOpenGLContext");
            [m_loadCtx makeCurrentContext];
        }
        return m_dataFactory;
    }
    
    void present()
    {
        [[m_nsContext openGLContext] flushBuffer];
    }
    
};

IGraphicsContext* _GraphicsContextCocoaGLNew(IGraphicsContext::EGraphicsAPI api,
                                             IWindow* parentWindow, NSOpenGLContext* lastGLCtx)
{
    if (api != IGraphicsContext::API_OPENGL_3_3 && api != IGraphicsContext::API_OPENGL_4_2)
        return NULL;
    
    /* Create temporary context to query GL version */
    NSOpenGLPixelFormat* nspf = [[NSOpenGLPixelFormat alloc] initWithAttributes:PF_RGBA8_ATTRS];
    if (!nspf)
        return NULL;
    NSOpenGLContext* nsctx = [[NSOpenGLContext alloc] initWithFormat:nspf shareContext:nil];
    [nspf release];
    if (!nsctx)
        return NULL;
    [nsctx makeCurrentContext];
    const char* glVersion = (char*)glGetString(GL_VERSION);
    unsigned major = 0;
    unsigned minor = 0;
    if (glVersion)
    {
        major = glVersion[0] - '0';
        minor = glVersion[2] - '0';
    }
    [NSOpenGLContext clearCurrentContext];
    [nsctx release];
    if (!glVersion)
        return NULL;
    
    if (major > 4 || (major == 4 && minor >= 2))
        api = IGraphicsContext::API_OPENGL_4_2;
    else if (major == 3 && minor >= 3)
        if (api == IGraphicsContext::API_OPENGL_4_2)
            return NULL;
    
    return new GraphicsContextCocoaGL(api, parentWindow, lastGLCtx);
}
    
class GraphicsContextCocoaMetal : public GraphicsContextCocoa
{
    GraphicsContextCocoaMetalInternal* m_nsContext = nullptr;
    
    IGraphicsCommandQueue* m_commandQueue = nullptr;
    IGraphicsDataFactory* m_dataFactory = nullptr;
    
public:
    MetalContext* m_metalCtx;

    GraphicsContextCocoaMetal(EGraphicsAPI api, IWindow* parentWindow,
                              MetalContext* metalCtx)
    : GraphicsContextCocoa(api, PF_RGBA8, parentWindow),
      m_metalCtx(metalCtx)
    {
        m_dataFactory = new MetalDataFactory(this, metalCtx);
    }
    
    ~GraphicsContextCocoaMetal()
    {
        delete m_dataFactory;
        delete m_commandQueue;
        [m_nsContext release];
        m_metalCtx->m_windows.erase(m_parentWindow);
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
        MetalContext::Window& w = m_metalCtx->m_windows[m_parentWindow];
        m_nsContext = [[GraphicsContextCocoaMetalInternal alloc] initWithBooContext:this];
        if (!m_nsContext)
            Log.report(LogVisor::FatalError, "unable to make new NSView for Metal");
        w.m_metalLayer = (CAMetalLayer*)m_nsContext.layer;
        [(NSWindow*)m_parentWindow->getPlatformHandle() setContentView:m_nsContext];
        CVDisplayLinkCreateWithActiveCGDisplays(&m_dispLink);
        CVDisplayLinkSetOutputCallback(m_dispLink, (CVDisplayLinkOutputCallback)DLCallback, this);
        CVDisplayLinkStart(m_dispLink);
        m_commandQueue = _NewMetalCommandQueue(m_metalCtx, m_parentWindow, this);
    }
    
    void makeCurrent()
    {
    }
    
    void postInit()
    {
    }
    
    IGraphicsCommandQueue* getCommandQueue()
    {
        return m_commandQueue;
    }
    
    IGraphicsDataFactory* getDataFactory()
    {
        return m_dataFactory;
    }
    
    IGraphicsDataFactory* getLoadContextDataFactory()
    {
        return m_dataFactory;
    }
    
    void present()
    {
    }
    
};

IGraphicsContext* _GraphicsContextCocoaMetalNew(IGraphicsContext::EGraphicsAPI api,
                                                IWindow* parentWindow,
                                                MetalContext* metalCtx)
{
    if (api != IGraphicsContext::API_METAL)
        return nullptr;
    return new GraphicsContextCocoaMetal(api, parentWindow, metalCtx);
}

}

@implementation BooCocoaResponder
- (id)initWithBooContext:(boo::GraphicsContextCocoa*)bctx View:(NSView*)view
{
    lastModifiers = 0;
    booContext = bctx;
    parentView = view;
    return self;
}

static inline boo::EModifierKey getMod(NSUInteger flags)
{
    int ret = boo::MKEY_NONE;
    if (flags & NSControlKeyMask)
        ret |= boo::MKEY_CTRL;
    if (flags & NSAlternateKeyMask)
        ret |= boo::MKEY_ALT;
    if (flags & NSShiftKeyMask)
        ret |= boo::MKEY_SHIFT;
    if (flags & NSCommandKeyMask)
        ret |= boo::MKEY_COMMAND;
    return static_cast<boo::EModifierKey>(ret);
}

static inline boo::EMouseButton getButton(NSEvent* event)
{
    NSInteger buttonNumber = event.buttonNumber;
    if (buttonNumber == 3)
        return boo::BUTTON_MIDDLE;
    else if (buttonNumber == 4)
        return boo::BUTTON_AUX1;
    else if (buttonNumber == 5)
        return boo::BUTTON_AUX2;
    return boo::BUTTON_NONE;
}

- (void)mouseDown:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
    float pixelFactor = [[parentView window] backingScaleFactor];
    NSRect frame = [parentView frame];
    boo::SWindowCoord coord =
    {
        {(unsigned)(liw.x * pixelFactor), (unsigned)(liw.y * pixelFactor)},
        {(unsigned)liw.x, (unsigned)liw.y},
        {(float)(liw.x / frame.size.width), (float)(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseDown(coord, boo::BUTTON_PRIMARY,
                                      getMod([theEvent modifierFlags]));
}

- (void)mouseUp:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
    float pixelFactor = [[parentView window] backingScaleFactor];
    NSRect frame = [parentView frame];
    boo::SWindowCoord coord =
    {
        {(unsigned)(liw.x * pixelFactor), (unsigned)(liw.y * pixelFactor)},
        {(unsigned)liw.x, (unsigned)liw.y},
        {(float)(liw.x / frame.size.width), (float)(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseUp(coord, boo::BUTTON_PRIMARY,
                                    getMod([theEvent modifierFlags]));
}

- (void)rightMouseDown:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
    float pixelFactor = [[parentView window] backingScaleFactor];
    NSRect frame = [parentView frame];
    boo::SWindowCoord coord =
    {
        {(unsigned)(liw.x * pixelFactor), (unsigned)(liw.y * pixelFactor)},
        {(unsigned)liw.x, (unsigned)liw.y},
        {(float)(liw.x / frame.size.width), (float)(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseDown(coord, boo::BUTTON_SECONDARY,
                                      getMod([theEvent modifierFlags]));
}

- (void)rightMouseUp:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
    float pixelFactor = [[parentView window] backingScaleFactor];
    NSRect frame = [parentView frame];
    boo::SWindowCoord coord =
    {
        {(unsigned)(liw.x * pixelFactor), (unsigned)(liw.y * pixelFactor)},
        {(unsigned)liw.x, (unsigned)liw.y},
        {(float)(liw.x / frame.size.width), (float)(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseUp(coord, boo::BUTTON_SECONDARY,
                                    getMod([theEvent modifierFlags]));
}

- (void)otherMouseDown:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    boo::EMouseButton button = getButton(theEvent);
    if (!button)
        return;
    NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
    float pixelFactor = [[parentView window] backingScaleFactor];
    NSRect frame = [parentView frame];
    boo::SWindowCoord coord =
    {
        {(unsigned)(liw.x * pixelFactor), (unsigned)(liw.y * pixelFactor)},
        {(unsigned)liw.x, (unsigned)liw.y},
        {(float)(liw.x / frame.size.width), (float)(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseDown(coord, button, getMod([theEvent modifierFlags]));
}

- (void)otherMouseUp:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    boo::EMouseButton button = getButton(theEvent);
    if (!button)
        return;
    NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
    float pixelFactor = [[parentView window] backingScaleFactor];
    NSRect frame = [parentView frame];
    boo::SWindowCoord coord =
    {
        {(unsigned)(liw.x * pixelFactor), (unsigned)(liw.y * pixelFactor)},
        {(unsigned)liw.x, (unsigned)liw.y},
        {(float)(liw.x / frame.size.width), (float)(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseUp(coord, button, getMod([theEvent modifierFlags]));
}

- (void)mouseMoved:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
    if (theEvent.window == [parentView window] && NSPointInRect(liw, parentView.frame))
    {
        float pixelFactor = [[parentView window] backingScaleFactor];
        NSRect frame = [parentView frame];
        boo::SWindowCoord coord =
        {
            {(unsigned)(liw.x * pixelFactor), (unsigned)(liw.y * pixelFactor)},
            {(unsigned)liw.x, (unsigned)liw.y},
            {(float)(liw.x / frame.size.width), (float)(liw.y / frame.size.height)}
        };
        booContext->m_callback->mouseMove(coord);
    }
}

- (void)mouseDragged:(NSEvent*)theEvent
{
    [self mouseMoved:theEvent];
}

- (void)rightMouseDragged:(NSEvent*)theEvent
{
    [self mouseMoved:theEvent];
}

- (void)otherMouseDragged:(NSEvent*)theEvent
{
    [self mouseMoved:theEvent];
}

- (void)scrollWheel:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
    float pixelFactor = [[parentView window] backingScaleFactor];
    NSRect frame = [parentView frame];
    boo::SWindowCoord coord =
    {
        {(unsigned)(liw.x * pixelFactor), (unsigned)(liw.y * pixelFactor)},
        {(unsigned)liw.x, (unsigned)liw.y},
        {(float)(liw.x / frame.size.width), (float)(liw.y / frame.size.height)}
    };
    boo::SScrollDelta scroll =
    {
        {(float)[theEvent scrollingDeltaX], (float)[theEvent scrollingDeltaY]},
        (bool)[theEvent hasPreciseScrollingDeltas]
    };
    booContext->m_callback->scroll(coord, scroll);
}

- (void)touchesBeganWithEvent:(NSEvent*)event
{
    if (!booContext->m_callback)
        return;
    for (NSTouch* touch in [event touchesMatchingPhase:NSTouchPhaseBegan inView:nil])
    {
        NSPoint pos = touch.normalizedPosition;
        boo::STouchCoord coord =
        {
            {(float)pos.x, (float)pos.y}
        };
        booContext->m_callback->touchDown(coord, (uintptr_t)touch.identity);
    }
}

- (void)touchesEndedWithEvent:(NSEvent*)event
{
    if (!booContext->m_callback)
        return;
    for (NSTouch* touch in [event touchesMatchingPhase:NSTouchPhaseEnded inView:nil])
    {
        NSPoint pos = touch.normalizedPosition;
        boo::STouchCoord coord =
        {
            {(float)pos.x, (float)pos.y}
        };
        booContext->m_callback->touchUp(coord, (uintptr_t)touch.identity);
    }
}

- (void)touchesMovedWithEvent:(NSEvent*)event
{
    if (!booContext->m_callback)
        return;
    for (NSTouch* touch in [event touchesMatchingPhase:NSTouchPhaseMoved inView:nil])
    {
        NSPoint pos = touch.normalizedPosition;
        boo::STouchCoord coord =
        {
            {(float)pos.x, (float)pos.y}
        };
        booContext->m_callback->touchMove(coord, (uintptr_t)touch.identity);
    }
}

- (void)touchesCancelledWithEvent:(NSEvent*)event
{
    if (!booContext->m_callback)
        return;
    for (NSTouch* touch in [event touchesMatchingPhase:NSTouchPhaseCancelled inView:nil])
    {
        NSPoint pos = touch.normalizedPosition;
        boo::STouchCoord coord =
        {
            {(float)pos.x, (float)pos.y}
        };
        booContext->m_callback->touchUp(coord, (uintptr_t)touch.identity);
    }
}

/* keycodes for keys that are independent of keyboard layout*/
enum
{
    kVK_Return                    = 0x24,
    kVK_Tab                       = 0x30,
    kVK_Space                     = 0x31,
    kVK_Delete                    = 0x33,
    kVK_Escape                    = 0x35,
    kVK_Command                   = 0x37,
    kVK_Shift                     = 0x38,
    kVK_CapsLock                  = 0x39,
    kVK_Option                    = 0x3A,
    kVK_Control                   = 0x3B,
    kVK_RightShift                = 0x3C,
    kVK_RightOption               = 0x3D,
    kVK_RightControl              = 0x3E,
    kVK_Function                  = 0x3F,
    kVK_F17                       = 0x40,
    kVK_VolumeUp                  = 0x48,
    kVK_VolumeDown                = 0x49,
    kVK_Mute                      = 0x4A,
    kVK_F18                       = 0x4F,
    kVK_F19                       = 0x50,
    kVK_F20                       = 0x5A,
    kVK_F5                        = 0x60,
    kVK_F6                        = 0x61,
    kVK_F7                        = 0x62,
    kVK_F3                        = 0x63,
    kVK_F8                        = 0x64,
    kVK_F9                        = 0x65,
    kVK_F11                       = 0x67,
    kVK_F13                       = 0x69,
    kVK_F16                       = 0x6A,
    kVK_F14                       = 0x6B,
    kVK_F10                       = 0x6D,
    kVK_F12                       = 0x6F,
    kVK_F15                       = 0x71,
    kVK_Help                      = 0x72,
    kVK_Home                      = 0x73,
    kVK_PageUp                    = 0x74,
    kVK_ForwardDelete             = 0x75,
    kVK_F4                        = 0x76,
    kVK_End                       = 0x77,
    kVK_F2                        = 0x78,
    kVK_PageDown                  = 0x79,
    kVK_F1                        = 0x7A,
    kVK_LeftArrow                 = 0x7B,
    kVK_RightArrow                = 0x7C,
    kVK_DownArrow                 = 0x7D,
    kVK_UpArrow                   = 0x7E
};
static boo::ESpecialKey translateKeycode(short code)
{
    switch (code) {
        case kVK_F1:
            return boo::KEY_F1;
        case kVK_F2:
            return boo::KEY_F2;
        case kVK_F3:
            return boo::KEY_F3;
        case kVK_F4:
            return boo::KEY_F4;
        case kVK_F5:
            return boo::KEY_F5;
        case kVK_F6:
            return boo::KEY_F6;
        case kVK_F7:
            return boo::KEY_F7;
        case kVK_F8:
            return boo::KEY_F8;
        case kVK_F9:
            return boo::KEY_F9;
        case kVK_F10:
            return boo::KEY_F10;
        case kVK_F11:
            return boo::KEY_F11;
        case kVK_F12:
            return boo::KEY_F12;
        case kVK_Escape:
            return boo::KEY_ESC;
        case kVK_Return:
            return boo::KEY_ENTER;
        case kVK_Delete:
            return boo::KEY_BACKSPACE;
        case kVK_ForwardDelete:
            return boo::KEY_DELETE;
        case kVK_Home:
            return boo::KEY_HOME;
        case kVK_End:
            return boo::KEY_END;
        case kVK_PageUp:
            return boo::KEY_PGUP;
        case kVK_PageDown:
            return boo::KEY_PGDOWN;
        case kVK_LeftArrow:
            return boo::KEY_LEFT;
        case kVK_RightArrow:
            return boo::KEY_RIGHT;
        case kVK_UpArrow:
            return boo::KEY_UP;
        case kVK_DownArrow:
            return boo::KEY_DOWN;
        default:
            return boo::KEY_NONE;
    }
}

- (void)keyDown:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    NSString* chars = theEvent.characters;
    if ([chars length] == 0 ||
        [chars characterAtIndex:0] == '\n' ||
        [chars characterAtIndex:0] == '\r')
        booContext->m_callback->specialKeyDown(translateKeycode(theEvent.keyCode),
                                               getMod(theEvent.modifierFlags),
                                               theEvent.isARepeat);
    else
        booContext->m_callback->charKeyDown([chars characterAtIndex:0],
                                            getMod(theEvent.modifierFlags),
                                            theEvent.isARepeat);
}

- (void)keyUp:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    NSString* chars = theEvent.characters;
    if ([chars length] == 0)
        booContext->m_callback->specialKeyUp(translateKeycode(theEvent.keyCode),
                                             getMod(theEvent.modifierFlags));
    else
        booContext->m_callback->charKeyUp([chars characterAtIndex:0],
                                          getMod(theEvent.modifierFlags));
}

- (void)flagsChanged:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    NSUInteger modFlags = theEvent.modifierFlags;
    if (modFlags != lastModifiers)
    {
        NSUInteger changedFlags = modFlags ^ lastModifiers;
        
        NSUInteger downFlags = changedFlags & modFlags;
        if (downFlags & NSControlKeyMask)
            booContext->m_callback->modKeyDown(boo::MKEY_CTRL, false);
        if (downFlags & NSAlternateKeyMask)
            booContext->m_callback->modKeyDown(boo::MKEY_ALT, false);
        if (downFlags & NSShiftKeyMask)
            booContext->m_callback->modKeyDown(boo::MKEY_SHIFT, false);
        if (downFlags & NSCommandKeyMask)
            booContext->m_callback->modKeyDown(boo::MKEY_COMMAND, false);
        
        NSUInteger upFlags = changedFlags & ~modFlags;
        if (upFlags & NSControlKeyMask)
            booContext->m_callback->modKeyUp(boo::MKEY_CTRL);
        if (upFlags & NSAlternateKeyMask)
            booContext->m_callback->modKeyUp(boo::MKEY_ALT);
        if (upFlags & NSShiftKeyMask)
            booContext->m_callback->modKeyUp(boo::MKEY_SHIFT);
        if (upFlags & NSCommandKeyMask)
            booContext->m_callback->modKeyUp(boo::MKEY_COMMAND);
        
        lastModifiers = modFlags;
    }
}

- (BOOL)acceptsTouchEvents
{
    return YES;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

@end
    
@implementation GraphicsContextCocoaGLInternal
- (id)initWithBooContext:(boo::GraphicsContextCocoaGL*)bctx
{
    resp = [[BooCocoaResponder alloc] initWithBooContext:bctx View:self];
    boo::IGraphicsContext::EPixelFormat pf = bctx->getPixelFormat();
    NSOpenGLPixelFormat* nspf = [[NSOpenGLPixelFormat alloc] initWithAttributes:PF_TABLE[pf]];
    self = [self initWithFrame:NSMakeRect(0, 0, 100, 100) pixelFormat:nspf];
    if (bctx->m_lastCtx)
    {
        NSOpenGLContext* sharedCtx = [[NSOpenGLContext alloc] initWithFormat:nspf shareContext:bctx->m_lastCtx];
        [self setOpenGLContext:sharedCtx];
        [sharedCtx setView:self];
    }
    [nspf release];
    return self;
}

- (void)dealloc
{
    [resp release];
    [super dealloc];
}

- (void)reshape
{
    boo::SWindowRect rect = {{int(self.frame.origin.x), int(self.frame.origin.y)},
                             {int(self.frame.size.width), int(self.frame.size.height)}};
    resp->booContext->m_callback->resized(rect);
    [super reshape];
}

- (BOOL)acceptsTouchEvents
{
    return YES;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (NSResponder*)nextResponder
{
    return resp;
}

@end

@implementation GraphicsContextCocoaMetalInternal
- (id)initWithBooContext:(boo::GraphicsContextCocoaMetal*)bctx
{
    self = [self initWithFrame:NSMakeRect(0, 0, 100, 100)];
    m_ctx = bctx->m_metalCtx;
    resp = [[BooCocoaResponder alloc] initWithBooContext:bctx View:self];
    return self;
}

- (void)dealloc
{
    [resp release];
    [super dealloc];
}

- (BOOL)wantsLayer
{
    return YES;
}

- (CALayer*)makeBackingLayer
{
    CAMetalLayer* layer = [CAMetalLayer new];
    layer.device = m_ctx->m_dev.get();
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO;
    return layer;
}

- (BOOL)acceptsTouchEvents
{
    return YES;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (NSResponder*)nextResponder
{
    return resp;
}

@end

namespace boo
{

class WindowCocoa : public IWindow
{
    
    WindowCocoaInternal* m_nsWindow;
    IGraphicsContext* m_gfxCtx;

public:

    WindowCocoa(const std::string& title, NSOpenGLContext* lastGLCtx, MetalContext* metalCtx)
    {
        dispatch_sync(dispatch_get_main_queue(),
        ^{
            m_nsWindow = [[WindowCocoaInternal alloc] initWithBooWindow:this title:title];
            if (metalCtx->m_dev)
                m_gfxCtx = _GraphicsContextCocoaMetalNew(IGraphicsContext::API_METAL, this, metalCtx);
            else
                m_gfxCtx = _GraphicsContextCocoaGLNew(IGraphicsContext::API_OPENGL_3_3, this, lastGLCtx);
            m_gfxCtx->initializeContext();
        });
    }
    
    void _clearWindow()
    {
        m_nsWindow = nullptr;
    }
    
    ~WindowCocoa()
    {
        [m_nsWindow orderOut:nil];
        delete m_gfxCtx;
        [m_nsWindow release];
        APP->_deletedWindow(this);
    }
    
    void setCallback(IWindowCallback* cb)
    {
        m_gfxCtx->_setCallback(cb);
    }
    
    void showWindow()
    {
        dispatch_sync(dispatch_get_main_queue(),
        ^{
            [m_nsWindow makeKeyAndOrderFront:nil];
        });
    }
    
    void hideWindow()
    {
        dispatch_sync(dispatch_get_main_queue(),
        ^{
            [m_nsWindow orderOut:nil];
        });
    }
    
    std::string getTitle()
    {
        return [[m_nsWindow title] UTF8String];
    }
    
    void setTitle(const std::string& title)
    {
        dispatch_sync(dispatch_get_main_queue(),
        ^{
            [m_nsWindow setTitle:[[NSString stringWithUTF8String:title.c_str()] autorelease]];
        });
    }
    
    void setWindowFrameDefault()
    {
        dispatch_sync(dispatch_get_main_queue(),
        ^{
            NSScreen* mainScreen = [NSScreen mainScreen];
            NSRect scrFrame = mainScreen.frame;
            float x_off = scrFrame.size.width / 3.0;
            float y_off = scrFrame.size.height / 3.0;
            [m_nsWindow setFrame:NSMakeRect(x_off, y_off, x_off * 2.0, y_off * 2.0) display:NO];
        });
    }
    
    void getWindowFrame(float& xOut, float& yOut, float& wOut, float& hOut) const
    {
        NSRect wFrame = m_nsWindow.frame;
        xOut = wFrame.origin.x;
        yOut = wFrame.origin.y;
        wOut = wFrame.size.width;
        hOut = wFrame.size.height;
    }
    
    void getWindowFrame(int& xOut, int& yOut, int& wOut, int& hOut) const
    {
        NSRect wFrame = m_nsWindow.frame;
        xOut = wFrame.origin.x;
        yOut = wFrame.origin.y;
        wOut = wFrame.size.width;
        hOut = wFrame.size.height;
    }
    
    void setWindowFrame(float x, float y, float w, float h)
    {
        dispatch_sync(dispatch_get_main_queue(),
        ^{
            NSRect wFrame = NSMakeRect(x, y, w, h);
            [m_nsWindow setFrame:wFrame display:NO];
        });
    }
    
    void setWindowFrame(int x, int y, int w, int h)
    {
        dispatch_sync(dispatch_get_main_queue(),
        ^{
            NSRect wFrame = NSMakeRect(x, y, w, h);
            [m_nsWindow setFrame:wFrame display:NO];
        });
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
            dispatch_sync(dispatch_get_main_queue(),
            ^{
                [m_nsWindow toggleFullScreen:nil];
            });
    }
    
    ETouchType getTouchType() const
    {
        return TOUCH_TRACKPAD;
    }
    
    void setStyle(EWindowStyle style)
    {
        if (style & STYLE_TITLEBAR)
            m_nsWindow.titleVisibility = NSWindowTitleVisible;
        else
            m_nsWindow.titleVisibility = NSWindowTitleHidden;
        
        if (style & STYLE_CLOSE)
            m_nsWindow.styleMask |= NSClosableWindowMask;
        else
            m_nsWindow.styleMask &= ~NSClosableWindowMask;
        
        if (style & STYLE_RESIZE)
            m_nsWindow.styleMask |= NSResizableWindowMask;
        else
            m_nsWindow.styleMask &= ~NSResizableWindowMask;
    }
    
    EWindowStyle getStyle() const
    {
        int retval = 0;
        retval |= m_nsWindow.titleVisibility == NSWindowTitleVisible ? STYLE_TITLEBAR : 0;
        retval |= (m_nsWindow.styleMask & NSClosableWindowMask) ? STYLE_CLOSE : 0;
        retval |= (m_nsWindow.styleMask & NSResizableWindowMask) ? STYLE_RESIZE: 0;
        return EWindowStyle(retval);
    }
    
    void waitForRetrace()
    {
        static_cast<GraphicsContextCocoa*>(m_gfxCtx)->waitForRetrace();
    }
    
    uintptr_t getPlatformHandle() const
    {
        return (uintptr_t)m_nsWindow;
    }
    
    IGraphicsCommandQueue* getCommandQueue()
    {
        return m_gfxCtx->getCommandQueue();
    }
    
    IGraphicsDataFactory* getDataFactory()
    {
        return m_gfxCtx->getDataFactory();
    }
    
    IGraphicsDataFactory* getLoadContextDataFactory()
    {
        return m_gfxCtx->getLoadContextDataFactory();
    }
    
};
    
IWindow* _WindowCocoaNew(const SystemString& title, NSOpenGLContext* lastGLCtx, MetalContext* metalCtx)
{
    return new WindowCocoa(title, lastGLCtx, metalCtx);
}
    
}

@implementation WindowCocoaInternal
- (id)initWithBooWindow:(boo::WindowCocoa *)bw title:(const boo::SystemString&)title
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

