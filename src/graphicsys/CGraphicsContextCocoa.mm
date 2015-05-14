#import <AppKit/AppKit.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include "windowsys/IGraphicsContext.hpp"
#include "windowsys/IWindow.hpp"

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

namespace boo {class CGraphicsContextCocoa;}
@interface CGraphicsContextCocoaInternal : NSOpenGLView
{
    NSUInteger lastModifiers;
    boo::CGraphicsContextCocoa* booContext;
}
- (id)initWithBooContext:(boo::CGraphicsContextCocoa*)bctx;
@end

namespace boo
{
    
class CGraphicsContextCocoa final : public IGraphicsContext
{
    
    EGraphicsAPI m_api;
    EPixelFormat m_pf;
    IWindow* m_parentWindow;
    CGraphicsContextCocoaInternal* m_nsContext;
    NSOpenGLContext* m_nsShareContext;
    
public:
    IWindowCallback* m_callback;
    
    CGraphicsContextCocoa(EGraphicsAPI api, IWindow* parentWindow)
    : m_api(api),
      m_pf(PF_RGBA8),
      m_parentWindow(parentWindow),
      m_nsContext(NULL),
      m_nsShareContext(NULL),
      m_callback(NULL)
    {}
    
    ~CGraphicsContextCocoa()
    {
        [m_nsContext release];
        [m_nsShareContext release];
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
        if (m_nsShareContext)
            return;
        m_nsContext = [[CGraphicsContextCocoaInternal alloc] initWithBooContext:this];
        [(NSWindow*)m_parentWindow->getPlatformHandle() setContentView:m_nsContext];
    }
    
    IGraphicsContext* makeShareContext() const
    {
        NSOpenGLContext* nsctx;
        if (m_nsContext)
        {
            nsctx = [[NSOpenGLContext alloc] initWithFormat:[m_nsContext pixelFormat]
                                               shareContext:[m_nsContext openGLContext]];
        }
        else if (m_nsShareContext)
        {
            nsctx = [[NSOpenGLContext alloc] initWithFormat:[m_nsShareContext pixelFormat]
                                               shareContext:m_nsShareContext];
        }
        else
            return NULL;
        if (!nsctx)
            return NULL;
        CGraphicsContextCocoa* newCtx = new CGraphicsContextCocoa(m_api, NULL);
        newCtx->m_nsShareContext = nsctx;
        return newCtx;
    }
    
    void makeCurrent()
    {
        if (m_nsContext)
            [[m_nsContext openGLContext] makeCurrentContext];
        else if (m_nsShareContext)
            [m_nsShareContext makeCurrentContext];
    }
    
    void clearCurrent()
    {
        [NSOpenGLContext clearCurrentContext];
    }
    
    void swapBuffer()
    {
        [[m_nsContext openGLContext] flushBuffer];
    }
    
};
    
IGraphicsContext* _CGraphicsContextCocoaNew(IGraphicsContext::EGraphicsAPI api,
                                            IWindow* parentWindow)
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

    return new CGraphicsContextCocoa(api, parentWindow);
}

}

@implementation CGraphicsContextCocoaInternal
- (id)initWithBooContext:(boo::CGraphicsContextCocoa*)bctx
{
    lastModifiers = 0;
    booContext = bctx;
    boo::IGraphicsContext::EPixelFormat pf = bctx->getPixelFormat();
    NSOpenGLPixelFormat* nspf = [[NSOpenGLPixelFormat alloc] initWithAttributes:PF_TABLE[pf]];
    self = [self initWithFrame:NSMakeRect(0, 0, 100, 100) pixelFormat:nspf];
    [nspf release];
    return self;
}

- (BOOL)acceptsTouchEvents
{
    return YES;
}

static inline boo::IWindowCallback::EModifierKey getMod(NSEventModifierFlags flags)
{
    int ret = boo::IWindowCallback::MKEY_NONE;
    if (flags & NSControlKeyMask)
        ret |= boo::IWindowCallback::MKEY_CTRL;
    if (flags & NSAlternateKeyMask)
        ret |= boo::IWindowCallback::MKEY_ALT;
    if (flags & NSShiftKeyMask)
        ret |= boo::IWindowCallback::MKEY_SHIFT;
    if (flags & NSCommandKeyMask)
        ret |= boo::IWindowCallback::MKEY_COMMAND;
    return static_cast<boo::IWindowCallback::EModifierKey>(ret);
}

static inline boo::IWindowCallback::EMouseButton getButton(NSEvent* event)
{
    NSInteger buttonNumber = event.buttonNumber;
    if (buttonNumber == 3)
        return boo::IWindowCallback::BUTTON_MIDDLE;
    else if (buttonNumber == 4)
        return boo::IWindowCallback::BUTTON_AUX1;
    else if (buttonNumber == 5)
        return boo::IWindowCallback::BUTTON_AUX2;
    return boo::IWindowCallback::BUTTON_NONE;
}

- (void)mouseDown:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    NSPoint liw = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    float pixelFactor = [[self window] backingScaleFactor];
    NSRect frame = [self frame];
    boo::IWindowCallback::SWindowCoord coord =
    {
        {(unsigned)(liw.x * pixelFactor), (unsigned)(liw.y * pixelFactor)},
        {(unsigned)liw.x, (unsigned)liw.y},
        {(float)(liw.x / frame.size.width), (float)(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseDown(coord, boo::IWindowCallback::BUTTON_PRIMARY,
                                      getMod([theEvent modifierFlags]));
}

- (void)mouseUp:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    NSPoint liw = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    float pixelFactor = [[self window] backingScaleFactor];
    NSRect frame = [self frame];
    boo::IWindowCallback::SWindowCoord coord =
    {
        {(unsigned)(liw.x * pixelFactor), (unsigned)(liw.y * pixelFactor)},
        {(unsigned)liw.x, (unsigned)liw.y},
        {(float)(liw.x / frame.size.width), (float)(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseUp(coord, boo::IWindowCallback::BUTTON_PRIMARY,
                                    getMod([theEvent modifierFlags]));
}

- (void)rightMouseDown:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    NSPoint liw = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    float pixelFactor = [[self window] backingScaleFactor];
    NSRect frame = [self frame];
    boo::IWindowCallback::SWindowCoord coord =
    {
        {(unsigned)(liw.x * pixelFactor), (unsigned)(liw.y * pixelFactor)},
        {(unsigned)liw.x, (unsigned)liw.y},
        {(float)(liw.x / frame.size.width), (float)(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseDown(coord, boo::IWindowCallback::BUTTON_SECONDARY,
                                      getMod([theEvent modifierFlags]));
}

- (void)rightMouseUp:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    NSPoint liw = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    float pixelFactor = [[self window] backingScaleFactor];
    NSRect frame = [self frame];
    boo::IWindowCallback::SWindowCoord coord =
    {
        {(unsigned)(liw.x * pixelFactor), (unsigned)(liw.y * pixelFactor)},
        {(unsigned)liw.x, (unsigned)liw.y},
        {(float)(liw.x / frame.size.width), (float)(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseUp(coord, boo::IWindowCallback::BUTTON_SECONDARY,
                                    getMod([theEvent modifierFlags]));
}

- (void)otherMouseDown:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    boo::IWindowCallback::EMouseButton button = getButton(theEvent);
    if (!button)
        return;
    NSPoint liw = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    float pixelFactor = [[self window] backingScaleFactor];
    NSRect frame = [self frame];
    boo::IWindowCallback::SWindowCoord coord =
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
    boo::IWindowCallback::EMouseButton button = getButton(theEvent);
    if (!button)
        return;
    NSPoint liw = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    float pixelFactor = [[self window] backingScaleFactor];
    NSRect frame = [self frame];
    boo::IWindowCallback::SWindowCoord coord =
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
    NSPoint liw = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    float pixelFactor = [[self window] backingScaleFactor];
    NSRect frame = [self frame];
    boo::IWindowCallback::SWindowCoord coord =
    {
        {(unsigned)(liw.x * pixelFactor), (unsigned)(liw.y * pixelFactor)},
        {(unsigned)liw.x, (unsigned)liw.y},
        {(float)(liw.x / frame.size.width), (float)(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseMove(coord);
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
    boo::IWindowCallback::SScrollDelta scroll =
    {
        {(float)[theEvent scrollingDeltaX], (float)[theEvent scrollingDeltaY]},
        (bool)[theEvent hasPreciseScrollingDeltas]
    };
    booContext->m_callback->scroll(scroll);
}

- (void)touchesBeganWithEvent:(NSEvent*)event
{
    if (!booContext->m_callback)
        return;
    for (NSTouch* touch in [event touchesMatchingPhase:NSTouchPhaseBegan inView:nil])
    {
        NSPoint pos = touch.normalizedPosition;
        boo::IWindowCallback::SWindowCoord coord =
        {
            {0, 0},
            {0, 0},
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
        boo::IWindowCallback::SWindowCoord coord =
        {
            {0, 0},
            {0, 0},
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
        boo::IWindowCallback::SWindowCoord coord =
        {
            {0, 0},
            {0, 0},
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
        boo::IWindowCallback::SWindowCoord coord =
        {
            {0, 0},
            {0, 0},
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
static boo::IWindowCallback::ESpecialKey translateKeycode(short code)
{
    switch (code) {
        case kVK_F1:
            return boo::IWindowCallback::KEY_F1;
        case kVK_F2:
            return boo::IWindowCallback::KEY_F2;
        case kVK_F3:
            return boo::IWindowCallback::KEY_F3;
        case kVK_F4:
            return boo::IWindowCallback::KEY_F4;
        case kVK_F5:
            return boo::IWindowCallback::KEY_F5;
        case kVK_F6:
            return boo::IWindowCallback::KEY_F6;
        case kVK_F7:
            return boo::IWindowCallback::KEY_F7;
        case kVK_F8:
            return boo::IWindowCallback::KEY_F8;
        case kVK_F9:
            return boo::IWindowCallback::KEY_F9;
        case kVK_F10:
            return boo::IWindowCallback::KEY_F10;
        case kVK_F11:
            return boo::IWindowCallback::KEY_F11;
        case kVK_F12:
            return boo::IWindowCallback::KEY_F12;
        case kVK_Escape:
            return boo::IWindowCallback::KEY_ESC;
        case kVK_Return:
            return boo::IWindowCallback::KEY_ENTER;
        case kVK_Delete:
            return boo::IWindowCallback::KEY_BACKSPACE;
        case kVK_ForwardDelete:
            return boo::IWindowCallback::KEY_DELETE;
        case kVK_Home:
            return boo::IWindowCallback::KEY_HOME;
        case kVK_End:
            return boo::IWindowCallback::KEY_END;
        case kVK_PageUp:
            return boo::IWindowCallback::KEY_PGUP;
        case kVK_PageDown:
            return boo::IWindowCallback::KEY_PGDOWN;
        case kVK_LeftArrow:
            return boo::IWindowCallback::KEY_LEFT;
        case kVK_RightArrow:
            return boo::IWindowCallback::KEY_RIGHT;
        case kVK_UpArrow:
            return boo::IWindowCallback::KEY_UP;
        case kVK_DownArrow:
            return boo::IWindowCallback::KEY_DOWN;
        default:
            return boo::IWindowCallback::KEY_NONE;
    }
}

- (void)keyDown:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    NSString* chars = theEvent.characters;
    if ([chars length] == 0)
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
    bool isRepeat = theEvent.isARepeat;
    if (modFlags != lastModifiers)
    {
        NSUInteger changedFlags = modFlags ^ lastModifiers;
        
        NSUInteger downFlags = changedFlags & modFlags;
        if (downFlags & NSControlKeyMask)
            booContext->m_callback->modKeyDown(boo::IWindowCallback::MKEY_CTRL, isRepeat);
        if (downFlags & NSAlternateKeyMask)
            booContext->m_callback->modKeyDown(boo::IWindowCallback::MKEY_ALT, isRepeat);
        if (downFlags & NSShiftKeyMask)
            booContext->m_callback->modKeyDown(boo::IWindowCallback::MKEY_SHIFT, isRepeat);
        if (downFlags & NSCommandKeyMask)
            booContext->m_callback->modKeyDown(boo::IWindowCallback::MKEY_COMMAND, isRepeat);
        
        NSUInteger upFlags = changedFlags & ~modFlags;
        if (upFlags & NSControlKeyMask)
            booContext->m_callback->modKeyUp(boo::IWindowCallback::MKEY_CTRL);
        if (upFlags & NSAlternateKeyMask)
            booContext->m_callback->modKeyUp(boo::IWindowCallback::MKEY_ALT);
        if (upFlags & NSShiftKeyMask)
            booContext->m_callback->modKeyUp(boo::IWindowCallback::MKEY_SHIFT);
        if (upFlags & NSCommandKeyMask)
            booContext->m_callback->modKeyUp(boo::IWindowCallback::MKEY_COMMAND);
        
        lastModifiers = modFlags;
    }
}

@end

