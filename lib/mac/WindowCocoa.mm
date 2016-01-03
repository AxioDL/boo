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

#if !__has_feature(objc_arc)
#error ARC Required
#endif

namespace boo {class WindowCocoa; class GraphicsContextCocoa;}
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

@interface BooCocoaResponder : NSResponder <NSTextInputClient>
{
@public
    NSUInteger lastModifiers;
    boo::GraphicsContextCocoa* booContext;
    NSView* parentView;
    NSTextInputContext* textContext;
}
- (id)initWithBooContext:(boo::GraphicsContextCocoa*)bctx View:(NSView*)view;
@end

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
    
    static CVReturn DLCallback(CVDisplayLinkRef displayLink,
                               const CVTimeStamp * inNow,
                               const CVTimeStamp * inOutputTime,
                               CVOptionFlags flagsIn,
                               CVOptionFlags * flagsOut,
                               GraphicsContextCocoa* ctx)
    {
        ctx->m_dlcv.notify_one();
        return kCVReturnSuccess;
    }
    
public:
    ~GraphicsContextCocoa()
    {
        if (m_dispLink)
        {
            CVDisplayLinkStop(m_dispLink);
            CVDisplayLinkRelease(m_dispLink);
        }
    }
    
    IWindowCallback* m_callback = nullptr;
    void waitForRetrace()
    {
        std::unique_lock<std::mutex> lk(m_dlmt);
        m_dlcv.wait(lk);
    }
    virtual BooCocoaResponder* responder() const=0;
};
class GraphicsContextCocoaGL;
class GraphicsContextCocoaMetal;
}

@interface GraphicsContextCocoaGLInternal : NSOpenGLView
{
@public
    BooCocoaResponder* resp;
}
- (id)initWithBooContext:(boo::GraphicsContextCocoaGL*)bctx;
@end

@interface GraphicsContextCocoaMetalInternal : NSView
{
@public
    BooCocoaResponder* resp;
    boo::MetalContext* m_ctx;
    boo::IWindow* m_window;
}
- (id)initWithBooContext:(boo::GraphicsContextCocoaMetal*)bctx;
- (void)reshapeHandler;
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
    NSOpenGLContext* m_mainCtx = nullptr;
    NSOpenGLContext* m_loadCtx = nullptr;
    
public:
    NSOpenGLContext* m_lastCtx = nullptr;
    
    GraphicsContextCocoaGL(EGraphicsAPI api, IWindow* parentWindow, NSOpenGLContext* lastGLCtx)
    : GraphicsContextCocoa(api, EPixelFormat::RGBA8, parentWindow),
      m_lastCtx(lastGLCtx)
    {
        m_dataFactory = new GLDataFactory(this);
    }
    
    ~GraphicsContextCocoaGL()
    {
        delete m_dataFactory;
        delete m_commandQueue;
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
    
    void initializeContext()
    {
        m_nsContext = [[GraphicsContextCocoaGLInternal alloc] initWithBooContext:this];
        if (!m_nsContext)
            Log.report(LogVisor::FatalError, "unable to make new NSOpenGLView");
        [(__bridge NSWindow*)(void*)m_parentWindow->getPlatformHandle() setContentView:m_nsContext];
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
    
    IGraphicsDataFactory* getMainContextDataFactory()
    {
        if (!m_mainCtx)
        {
            NSOpenGLPixelFormat* nspf = [[NSOpenGLPixelFormat alloc] initWithAttributes:PF_TABLE[int(m_pf)]];
            m_mainCtx = [[NSOpenGLContext alloc] initWithFormat:nspf shareContext:[m_nsContext openGLContext]];
            if (!m_mainCtx)
                Log.report(LogVisor::FatalError, "unable to make main NSOpenGLContext");
        }
        [m_mainCtx makeCurrentContext];
        return m_dataFactory;
    }
    
    IGraphicsDataFactory* getLoadContextDataFactory()
    {
        if (!m_loadCtx)
        {
            NSOpenGLPixelFormat* nspf = [[NSOpenGLPixelFormat alloc] initWithAttributes:PF_TABLE[int(m_pf)]];
            m_loadCtx = [[NSOpenGLContext alloc] initWithFormat:nspf shareContext:[m_nsContext openGLContext]];
            if (!m_loadCtx)
                Log.report(LogVisor::FatalError, "unable to make load NSOpenGLContext");
        }
        [m_loadCtx makeCurrentContext];
        return m_dataFactory;
    }
    
    void present()
    {
        [[m_nsContext openGLContext] flushBuffer];
    }
    
    BooCocoaResponder* responder() const
    {
        if (!m_nsContext)
            return nullptr;
        return m_nsContext->resp;
    }
    
};

IGraphicsContext* _GraphicsContextCocoaGLNew(IGraphicsContext::EGraphicsAPI api,
                                             IWindow* parentWindow, NSOpenGLContext* lastGLCtx)
{
    if (api != IGraphicsContext::EGraphicsAPI::OpenGL3_3 && api != IGraphicsContext::EGraphicsAPI::OpenGL4_2)
        return NULL;
    
    /* Create temporary context to query GL version */
    NSOpenGLPixelFormat* nspf = [[NSOpenGLPixelFormat alloc] initWithAttributes:PF_RGBA8_ATTRS];
    if (!nspf)
        return NULL;
    NSOpenGLContext* nsctx = [[NSOpenGLContext alloc] initWithFormat:nspf shareContext:nil];
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
    if (!glVersion)
        return NULL;
    
    if (major > 4 || (major == 4 && minor >= 2))
        api = IGraphicsContext::EGraphicsAPI::OpenGL4_2;
    else if (major == 3 && minor >= 3)
        if (api == IGraphicsContext::EGraphicsAPI::OpenGL4_2)
            return NULL;
    
    return new GraphicsContextCocoaGL(api, parentWindow, lastGLCtx);
}
    
#if BOO_HAS_METAL
class GraphicsContextCocoaMetal : public GraphicsContextCocoa
{
    GraphicsContextCocoaMetalInternal* m_nsContext = nullptr;
    
    IGraphicsCommandQueue* m_commandQueue = nullptr;
    IGraphicsDataFactory* m_dataFactory = nullptr;
    
public:
    IWindow* m_parentWindow;
    MetalContext* m_metalCtx;

    GraphicsContextCocoaMetal(EGraphicsAPI api, IWindow* parentWindow,
                              MetalContext* metalCtx)
    : GraphicsContextCocoa(api, EPixelFormat::RGBA8, parentWindow),
      m_parentWindow(parentWindow), m_metalCtx(metalCtx)
    {
        m_dataFactory = new MetalDataFactory(this, metalCtx);
    }
    
    ~GraphicsContextCocoaMetal()
    {
        delete m_dataFactory;
        delete m_commandQueue;
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
        if (pf > EPixelFormat::RGBAF32_Z24)
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
        [(__bridge NSWindow*)(void*)m_parentWindow->getPlatformHandle() setContentView:m_nsContext];
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
    
    IGraphicsDataFactory* getMainContextDataFactory()
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
    
    BooCocoaResponder* responder() const
    {
        if (!m_nsContext)
            return nullptr;
        return m_nsContext->resp;
    }
    
};

IGraphicsContext* _GraphicsContextCocoaMetalNew(IGraphicsContext::EGraphicsAPI api,
                                                IWindow* parentWindow,
                                                MetalContext* metalCtx)
{
    if (api != IGraphicsContext::EGraphicsAPI::Metal)
        return nullptr;
    return new GraphicsContextCocoaMetal(api, parentWindow, metalCtx);
}
#endif

}

@implementation BooCocoaResponder
- (id)initWithBooContext:(boo::GraphicsContextCocoa*)bctx View:(NSView*)view
{
    lastModifiers = 0;
    booContext = bctx;
    parentView = view;
    textContext = [[NSTextInputContext alloc] initWithClient:self];
    return self;
}

- (BOOL)hasMarkedText
{
    if (booContext->m_callback)
    {
        boo::ITextInputCallback* textCb = booContext->m_callback->getTextInputCallback();
        if (textCb)
            return textCb->hasMarkedText();
    }
    return false;
}

- (NSRange)markedRange
{
    if (booContext->m_callback)
    {
        boo::ITextInputCallback* textCb = booContext->m_callback->getTextInputCallback();
        if (textCb)
        {
            std::pair<int,int> rng = textCb->markedRange();
            return NSMakeRange(rng.first < 0 ? NSNotFound : rng.first, rng.second);
        }
    }
    return NSMakeRange(NSNotFound, 0);
}

- (NSRange)selectedRange
{
    if (booContext->m_callback)
    {
        boo::ITextInputCallback* textCb = booContext->m_callback->getTextInputCallback();
        if (textCb)
        {
            std::pair<int,int> rng = textCb->selectedRange();
            return NSMakeRange(rng.first < 0 ? NSNotFound : rng.first, rng.second);
        }
    }
    return NSMakeRange(NSNotFound, 0);
}

- (void)setMarkedText:(id)aString selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange
{
    if (booContext->m_callback)
    {
        boo::ITextInputCallback* textCb = booContext->m_callback->getTextInputCallback();
        if (textCb)
        {
            NSString* plainStr = aString;
            if ([aString isKindOfClass:[NSAttributedString class]])
                plainStr = ((NSAttributedString*)aString).string;
            textCb->setMarkedText([plainStr UTF8String],
                                  std::make_pair(selectedRange.location, selectedRange.length),
                                  std::make_pair(replacementRange.location==NSNotFound ? -1 : replacementRange.location,
                                                 replacementRange.length));
        }
    }
}

- (void)unmarkText
{
    if (booContext->m_callback)
    {
        boo::ITextInputCallback* textCb = booContext->m_callback->getTextInputCallback();
        if (textCb)
            textCb->unmarkText();
    }
}

- (NSArray<NSString*>*)validAttributesForMarkedText
{
    return @[];
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)aRange actualRange:(NSRangePointer)actualRange
{
    if (booContext->m_callback)
    {
        boo::ITextInputCallback* textCb = booContext->m_callback->getTextInputCallback();
        if (textCb)
        {
            std::pair<int,int> actualRng;
            std::string str = textCb->substringForRange(std::make_pair(aRange.location, aRange.length), actualRng);
            if (str.empty())
                return nil;
            actualRange->location = actualRng.first;
            actualRange->length = actualRng.second;
            NSString* nsStr = [NSString stringWithUTF8String:str.c_str()];
            NSAttributedString* ret = [[NSAttributedString alloc] initWithString:nsStr];
            return ret;
        }
    }
    return nil;
}

- (void)insertText:(id)aString replacementRange:(NSRange)replacementRange
{
    if (booContext->m_callback)
    {
        boo::ITextInputCallback* textCb = booContext->m_callback->getTextInputCallback();
        if (textCb)
        {
            NSString* plainStr = aString;
            if ([aString isKindOfClass:[NSAttributedString class]])
                plainStr = ((NSAttributedString*)aString).string;
            textCb->insertText([plainStr UTF8String],
                               std::make_pair(replacementRange.location == NSNotFound ? -1 : replacementRange.location,
                                              replacementRange.length));
        }
    }
}

- (NSUInteger)characterIndexForPoint:(NSPoint)aPoint
{
    if (booContext->m_callback)
    {
        boo::ITextInputCallback* textCb = booContext->m_callback->getTextInputCallback();
        if (textCb)
        {
            NSPoint backingPoint = [parentView convertPointToBacking:aPoint];
            boo::SWindowCoord coord = {{int(backingPoint.x), int(backingPoint.y)}, {int(aPoint.x), int(aPoint.y)}};
            int idx = textCb->characterIndexAtPoint(coord);
            if (idx < 0)
                return NSNotFound;
            return idx;
        }
    }
    return NSNotFound;
}

- (NSRect)firstRectForCharacterRange:(NSRange)aRange actualRange:(NSRangePointer)actualRange
{
    if (booContext->m_callback)
    {
        boo::ITextInputCallback* textCb = booContext->m_callback->getTextInputCallback();
        if (textCb)
        {
            std::pair<int,int> actualRng;
            boo::SWindowRect rect =
            textCb->rectForCharacterRange(std::make_pair(aRange.location, aRange.length), actualRng);
            actualRange->location = actualRng.first;
            actualRange->length = actualRng.second;
            return [[parentView window] convertRectToScreen:
             [parentView convertRectFromBacking:NSMakeRect(rect.location[0], rect.location[1],
                                                           rect.size[0], rect.size[1])]];
        }
    }
    return NSMakeRect(0, 0, 0, 0);
}

- (void)doCommandBySelector:(SEL)aSelector
{
}

static inline boo::EModifierKey getMod(NSUInteger flags)
{
    boo::EModifierKey ret = boo::EModifierKey::None;
    if (flags & NSControlKeyMask)
        ret |= boo::EModifierKey::Ctrl;
    if (flags & NSAlternateKeyMask)
        ret |= boo::EModifierKey::Alt;
    if (flags & NSShiftKeyMask)
        ret |= boo::EModifierKey::Shift;
    if (flags & NSCommandKeyMask)
        ret |= boo::EModifierKey::Command;
    return ret;
}

static inline boo::EMouseButton getButton(NSEvent* event)
{
    NSInteger buttonNumber = event.buttonNumber;
    if (buttonNumber == 3)
        return boo::EMouseButton::Middle;
    else if (buttonNumber == 4)
        return boo::EMouseButton::Aux1;
    else if (buttonNumber == 5)
        return boo::EMouseButton::Aux2;
    return boo::EMouseButton::None;
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
        {int(liw.x * pixelFactor), int(liw.y * pixelFactor)},
        {int(liw.x), int(liw.y)},
        {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseDown(coord, boo::EMouseButton::Primary,
                                      getMod([theEvent modifierFlags]));
    [textContext handleEvent:theEvent];
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
        {int(liw.x * pixelFactor), int(liw.y * pixelFactor)},
        {int(liw.x), int(liw.y)},
        {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseUp(coord, boo::EMouseButton::Primary,
                                    getMod([theEvent modifierFlags]));
    [textContext handleEvent:theEvent];
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
        {int(liw.x * pixelFactor), int(liw.y * pixelFactor)},
        {int(liw.x), int(liw.y)},
        {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseDown(coord, boo::EMouseButton::Secondary,
                                      getMod([theEvent modifierFlags]));
    [textContext handleEvent:theEvent];
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
        {int(liw.x * pixelFactor), int(liw.y * pixelFactor)},
        {int(liw.x), int(liw.y)},
        {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseUp(coord, boo::EMouseButton::Secondary,
                                    getMod([theEvent modifierFlags]));
    [textContext handleEvent:theEvent];
}

- (void)otherMouseDown:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    boo::EMouseButton button = getButton(theEvent);
    if (button == boo::EMouseButton::None)
        return;
    NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
    float pixelFactor = [[parentView window] backingScaleFactor];
    NSRect frame = [parentView frame];
    boo::SWindowCoord coord =
    {
        {int(liw.x * pixelFactor), int(liw.y * pixelFactor)},
        {int(liw.x), int(liw.y)},
        {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseDown(coord, button, getMod([theEvent modifierFlags]));
    [textContext handleEvent:theEvent];
}

- (void)otherMouseUp:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    boo::EMouseButton button = getButton(theEvent);
    if (button == boo::EMouseButton::None)
        return;
    NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
    float pixelFactor = [[parentView window] backingScaleFactor];
    NSRect frame = [parentView frame];
    boo::SWindowCoord coord =
    {
        {int(liw.x * pixelFactor), int(liw.y * pixelFactor)},
        {int(liw.x), int(liw.y)},
        {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseUp(coord, button, getMod([theEvent modifierFlags]));
    [textContext handleEvent:theEvent];
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
            {int(liw.x * pixelFactor), int(liw.y * pixelFactor)},
            {int(liw.x), int(liw.y)},
            {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
        };
        booContext->m_callback->mouseMove(coord);
    }
    [textContext handleEvent:theEvent];
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

- (void)mouseEntered:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
    float pixelFactor = [[parentView window] backingScaleFactor];
    NSRect frame = [parentView frame];
    boo::SWindowCoord coord =
    {
        {int(liw.x * pixelFactor), int(liw.y * pixelFactor)},
        {int(liw.x), int(liw.y)},
        {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseEnter(coord);
}

- (void)mouseExited:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
    float pixelFactor = [[parentView window] backingScaleFactor];
    NSRect frame = [parentView frame];
    boo::SWindowCoord coord =
    {
        {int(liw.x * pixelFactor), int(liw.y * pixelFactor)},
        {int(liw.x), int(liw.y)},
        {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
    };
    booContext->m_callback->mouseLeave(coord);
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
        {int(liw.x * pixelFactor), int(liw.y * pixelFactor)},
        {int(liw.x), int(liw.y)},
        {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
    };
    boo::SScrollDelta scroll =
    {
        {(float)[theEvent scrollingDeltaX], (float)[theEvent scrollingDeltaY]},
        (bool)[theEvent hasPreciseScrollingDeltas], true
    };
    booContext->m_callback->scroll(coord, scroll);
    [textContext handleEvent:theEvent];
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
    switch (code)
    {
        case kVK_F1:
            return boo::ESpecialKey::F1;
        case kVK_F2:
            return boo::ESpecialKey::F2;
        case kVK_F3:
            return boo::ESpecialKey::F3;
        case kVK_F4:
            return boo::ESpecialKey::F4;
        case kVK_F5:
            return boo::ESpecialKey::F5;
        case kVK_F6:
            return boo::ESpecialKey::F6;
        case kVK_F7:
            return boo::ESpecialKey::F7;
        case kVK_F8:
            return boo::ESpecialKey::F8;
        case kVK_F9:
            return boo::ESpecialKey::F9;
        case kVK_F10:
            return boo::ESpecialKey::F10;
        case kVK_F11:
            return boo::ESpecialKey::F11;
        case kVK_F12:
            return boo::ESpecialKey::F12;
        case kVK_Escape:
            return boo::ESpecialKey::Esc;
        case kVK_Return:
            return boo::ESpecialKey::Enter;
        case kVK_Delete:
            return boo::ESpecialKey::Backspace;
        case kVK_ForwardDelete:
            return boo::ESpecialKey::Delete;
        case kVK_Home:
            return boo::ESpecialKey::Home;
        case kVK_End:
            return boo::ESpecialKey::End;
        case kVK_PageUp:
            return boo::ESpecialKey::PgUp;
        case kVK_PageDown:
            return boo::ESpecialKey::PgDown;
        case kVK_LeftArrow:
            return boo::ESpecialKey::Left;
        case kVK_RightArrow:
            return boo::ESpecialKey::Right;
        case kVK_UpArrow:
            return boo::ESpecialKey::Up;
        case kVK_DownArrow:
            return boo::ESpecialKey::Down;
        default:
            return boo::ESpecialKey::None;
    }
}

- (void)keyDown:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    boo::ESpecialKey special = translateKeycode(theEvent.keyCode);
    boo::EModifierKey mods = getMod(theEvent.modifierFlags);
    NSString* chars;
    if ((mods & boo::EModifierKey::Ctrl) != boo::EModifierKey::None)
        chars = theEvent.charactersIgnoringModifiers;
    else
        chars = theEvent.characters;
    if (special != boo::ESpecialKey::None)
        booContext->m_callback->specialKeyDown(special,
                                               mods,
                                               theEvent.isARepeat);
    else if ([chars length])
        booContext->m_callback->charKeyDown([chars characterAtIndex:0],
                                            mods,
                                            theEvent.isARepeat);
    [textContext handleEvent:theEvent];
}

- (void)keyUp:(NSEvent*)theEvent
{
    if (!booContext->m_callback)
        return;
    boo::ESpecialKey special = translateKeycode(theEvent.keyCode);
    boo::EModifierKey mods = getMod(theEvent.modifierFlags);
    NSString* chars;
    if ((mods & boo::EModifierKey::Ctrl) != boo::EModifierKey::None)
        chars = theEvent.charactersIgnoringModifiers;
    else
        chars = theEvent.characters;
    if (special != boo::ESpecialKey::None)
        booContext->m_callback->specialKeyUp(special,
                                             mods);
    else if ([chars length])
        booContext->m_callback->charKeyUp([chars characterAtIndex:0],
                                          mods);
    //[textContext handleEvent:theEvent];
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
            booContext->m_callback->modKeyDown(boo::EModifierKey::Ctrl, false);
        if (downFlags & NSAlternateKeyMask)
            booContext->m_callback->modKeyDown(boo::EModifierKey::Alt, false);
        if (downFlags & NSShiftKeyMask)
            booContext->m_callback->modKeyDown(boo::EModifierKey::Shift, false);
        if (downFlags & NSCommandKeyMask)
            booContext->m_callback->modKeyDown(boo::EModifierKey::Command, false);
        
        NSUInteger upFlags = changedFlags & ~modFlags;
        if (upFlags & NSControlKeyMask)
            booContext->m_callback->modKeyUp(boo::EModifierKey::Ctrl);
        if (upFlags & NSAlternateKeyMask)
            booContext->m_callback->modKeyUp(boo::EModifierKey::Alt);
        if (upFlags & NSShiftKeyMask)
            booContext->m_callback->modKeyUp(boo::EModifierKey::Shift);
        if (upFlags & NSCommandKeyMask)
            booContext->m_callback->modKeyUp(boo::EModifierKey::Command);
        
        lastModifiers = modFlags;
    }
    [textContext handleEvent:theEvent];
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
    NSOpenGLPixelFormat* nspf = [[NSOpenGLPixelFormat alloc] initWithAttributes:PF_TABLE[int(pf)]];
    self = [self initWithFrame:NSMakeRect(0, 0, 100, 100) pixelFormat:nspf];
    if (bctx->m_lastCtx)
    {
        NSOpenGLContext* sharedCtx = [[NSOpenGLContext alloc] initWithFormat:nspf shareContext:bctx->m_lastCtx];
        [self setOpenGLContext:sharedCtx];
        [sharedCtx setView:self];
    }
    NSTrackingArea* trackingArea = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                                options:(NSTrackingMouseEnteredAndExited |
                                                                         NSTrackingMouseMoved |
                                                                         NSTrackingActiveAlways |
                                                                         NSTrackingInVisibleRect)
                                                                  owner:self
                                                               userInfo:nil];
    [self addTrackingArea:trackingArea];
    return self;
}

- (void)reshape
{
    boo::SWindowRect rect = {int(self.frame.origin.x), int(self.frame.origin.y),
                             int(self.frame.size.width), int(self.frame.size.height)};
    if (resp->booContext->m_callback)
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

#if BOO_HAS_METAL
@implementation GraphicsContextCocoaMetalInternal
- (id)initWithBooContext:(boo::GraphicsContextCocoaMetal*)bctx
{
    m_ctx = bctx->m_metalCtx;
    m_window = bctx->m_parentWindow;
    self = [self initWithFrame:NSMakeRect(0, 0, 100, 100)];
    [self setWantsLayer:YES];
    resp = [[BooCocoaResponder alloc] initWithBooContext:bctx View:self];
    NSTrackingArea* trackingArea = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                                options:(NSTrackingMouseEnteredAndExited |
                                                                         NSTrackingMouseMoved |
                                                                         NSTrackingActiveAlways |
                                                                         NSTrackingInVisibleRect)
                                                                  owner:self
                                                               userInfo:nil];
    [self addTrackingArea:trackingArea];
    return self;
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

- (void)reshapeHandler
{
    boo::SWindowRect rect = {int(self.frame.origin.x), int(self.frame.origin.y),
                             int(self.frame.size.width), int(self.frame.size.height)};
    boo::MetalContext::Window& w = m_ctx->m_windows[m_window];
    std::unique_lock<std::mutex> lk(w.m_resizeLock);
    if (resp->booContext->m_callback)
        resp->booContext->m_callback->resized(rect);
    w.m_size = CGSizeMake(rect.size[0], rect.size[1]);
    w.m_needsResize = YES;
}

- (void)setFrameSize:(NSSize)newSize
{
    [super setFrameSize:newSize];
    [self reshapeHandler];
}

- (void)setBoundsSize:(NSSize)newSize
{
    [super setBoundsSize:newSize];
    [self reshapeHandler];
}

- (void)viewDidChangeBackingProperties
{
    [super viewDidChangeBackingProperties];
    [self reshapeHandler];
}

@end
#endif

namespace boo
{
    
static NSString* ClipboardTypes[] =
{
    0, NSPasteboardTypeString, NSPasteboardTypeString, NSPasteboardTypePNG
};

class WindowCocoa : public IWindow
{
    
    WindowCocoaInternal* m_nsWindow;
    GraphicsContextCocoa* m_gfxCtx;
    EMouseCursor m_cursor = EMouseCursor::None;

public:

    WindowCocoa(const std::string& title, NSOpenGLContext* lastGLCtx, MetalContext* metalCtx)
    {
        dispatch_sync(dispatch_get_main_queue(),
        ^{
            m_nsWindow = [[WindowCocoaInternal alloc] initWithBooWindow:this title:title];
#if BOO_HAS_METAL
            if (metalCtx->m_dev)
                m_gfxCtx = static_cast<GraphicsContextCocoa*>(_GraphicsContextCocoaMetalNew(IGraphicsContext::EGraphicsAPI::Metal, this, metalCtx));
            else
#endif
                m_gfxCtx = static_cast<GraphicsContextCocoa*>(_GraphicsContextCocoaGLNew(IGraphicsContext::EGraphicsAPI::OpenGL3_3, this, lastGLCtx));
            m_gfxCtx->initializeContext();
        });
    }
    
    void _clearWindow()
    {
        /* Caller consumes reference on its own */
        (void)(__bridge_retained void*)m_nsWindow;
        m_nsWindow = nullptr;
    }
    
    ~WindowCocoa()
    {
        [m_nsWindow orderOut:nil];
        delete m_gfxCtx;
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
            [m_nsWindow setTitle:[NSString stringWithUTF8String:title.c_str()]];
        });
    }
    
    void setCursor(EMouseCursor cursor)
    {
        if (cursor == m_cursor)
            return;
        m_cursor = cursor;
        dispatch_async(dispatch_get_main_queue(),
        ^{
            switch (cursor)
            {
            case EMouseCursor::Pointer:
                [[NSCursor arrowCursor] set];
                break;
            case EMouseCursor::HorizontalArrow:
                [[NSCursor resizeLeftRightCursor] set];
                break;
            case EMouseCursor::VerticalArrow:
                [[NSCursor resizeUpDownCursor] set];
                break;
            case EMouseCursor::IBeam:
                [[NSCursor IBeamCursor] set];
                break;
            default: break;
            }
        });
    }
    
    void setWaitCursor(bool wait) {}
    
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
        NSRect wFrame = m_nsWindow.contentView.frame;
        xOut = wFrame.origin.x;
        yOut = wFrame.origin.y;
        wOut = wFrame.size.width;
        hOut = wFrame.size.height;
    }
    
    void getWindowFrame(int& xOut, int& yOut, int& wOut, int& hOut) const
    {
        NSRect wFrame = m_nsWindow.contentView.frame;
        xOut = wFrame.origin.x;
        yOut = wFrame.origin.y;
        wOut = wFrame.size.width;
        hOut = wFrame.size.height;
    }
    
    void setWindowFrame(float x, float y, float w, float h)
    {
        dispatch_sync(dispatch_get_main_queue(),
        ^{
            [m_nsWindow setContentSize:NSMakeSize(w, h)];
            [m_nsWindow setFrameOrigin:NSMakePoint(x, y)];
        });
    }
    
    void setWindowFrame(int x, int y, int w, int h)
    {
        dispatch_sync(dispatch_get_main_queue(),
        ^{
            [m_nsWindow setContentSize:NSMakeSize(w, h)];
            [m_nsWindow setFrameOrigin:NSMakePoint(x, y)];
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
    
    void claimKeyboardFocus(const int coord[2])
    {
        BooCocoaResponder* resp = m_gfxCtx->responder();
        if (resp)
        {
            dispatch_async(dispatch_get_main_queue(),
            ^{
                if (coord)
                    [resp->textContext activate];
                else
                    [resp->textContext deactivate];
            });
        }
    }
    
    bool clipboardCopy(EClipboardType type, const uint8_t* data, size_t sz)
    {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        NSData* d = [NSData dataWithBytes:data length:sz];
        [pb setData:d forType:ClipboardTypes[int(type)]];
        return true;
    }
    
    std::unique_ptr<uint8_t[]> clipboardPaste(EClipboardType type, size_t& sz)
    {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        NSData* d = [pb dataForType:ClipboardTypes[int(type)]];
        if (!d)
            return std::unique_ptr<uint8_t[]>();
        sz = [d length];
        std::unique_ptr<uint8_t[]> ret(new uint8_t[sz]);
        [d getBytes:ret.get() length:sz];
        return ret;
    }
    
    ETouchType getTouchType() const
    {
        return ETouchType::Trackpad;
    }
    
    void setStyle(EWindowStyle style)
    {
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 101000
        if ((style & EWindowStyle::Titlebar) != EWindowStyle::None)
            m_nsWindow.titleVisibility = NSWindowTitleVisible;
        else
            m_nsWindow.titleVisibility = NSWindowTitleHidden;
#endif
        
        if ((style & EWindowStyle::Close) != EWindowStyle::None)
            m_nsWindow.styleMask |= NSClosableWindowMask;
        else
            m_nsWindow.styleMask &= ~NSClosableWindowMask;
        
        if ((style & EWindowStyle::Resize) != EWindowStyle::None)
            m_nsWindow.styleMask |= NSResizableWindowMask;
        else
            m_nsWindow.styleMask &= ~NSResizableWindowMask;
    }
    
    EWindowStyle getStyle() const
    {
        EWindowStyle retval = EWindowStyle::None;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 101000
        retval |= m_nsWindow.titleVisibility == NSWindowTitleVisible ? EWindowStyle::Titlebar : EWindowStyle::None;
#else
        retval |= EWindowStyle::Titlebar;
#endif
        retval |= (m_nsWindow.styleMask & NSClosableWindowMask) ? EWindowStyle::Close : EWindowStyle::None;
        retval |= (m_nsWindow.styleMask & NSResizableWindowMask) ? EWindowStyle::Resize: EWindowStyle::None;
        return retval;
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
    
    IGraphicsDataFactory* getMainContextDataFactory()
    {
        return m_gfxCtx->getMainContextDataFactory();
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
    self.title = [NSString stringWithUTF8String:title.c_str()];
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

