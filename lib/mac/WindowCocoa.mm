#include "boo/graphicsdev/Metal.hpp"

#include "boo/IApplication.hpp"
#include "boo/IGraphicsContext.hpp"
#include "boo/IWindow.hpp"
#include "boo/audiodev/IAudioVoiceEngine.hpp"
#include "lib/mac/CocoaCommon.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>

#import <AppKit/AppKit.h>
#import <CoreVideo/CVDisplayLink.h>

#include <logvisor/logvisor.hpp>

#if !__has_feature(objc_arc)
#error ARC Required
#endif

#ifndef USE_PRIVATE_APIS
#define USE_PRIVATE_APIS 1
#endif

namespace boo {
class WindowCocoa;

class GraphicsContextCocoa;
}
@interface WindowCocoaInternal : NSWindow <NSWindowDelegate> {
  std::shared_ptr<boo::WindowCocoa> booWindow;
  id touchBarProvider;
}
- (id)initWithBooWindow:(std::shared_ptr<boo::WindowCocoa>&)bw title:(std::string_view)title;

- (void)setFrameDefault;

- (NSRect)genFrameDefault;

- (void)setTouchBarProvider:(id)provider;
@end

#if 0
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
    0, 0
};

static const NSOpenGLPixelFormatAttribute PF_RGBA16_ATTRS[] =
{
    NSOpenGLPFAAccelerated,
    NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFAColorSize, 48,
    NSOpenGLPFAAlphaSize, 16,
    0, 0
};

static const NSOpenGLPixelFormatAttribute PF_RGBA8_Z24_ATTRS[] =
{
    NSOpenGLPFAAccelerated,
    NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFAColorSize, 24,
    NSOpenGLPFAAlphaSize, 8,
    NSOpenGLPFADepthSize, 24,
    0, 0
};

static const NSOpenGLPixelFormatAttribute PF_RGBAF32_ATTRS[] =
{
    NSOpenGLPFAAccelerated,
    NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFAColorFloat,
    NSOpenGLPFAColorSize, 96,
    NSOpenGLPFAAlphaSize, 32,
    0, 0
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
    0, 0
};

static const NSOpenGLPixelFormatAttribute* PF_TABLE[] =
{
    nullptr,
    PF_RGBA8_ATTRS,
    PF_RGBA16_ATTRS,
    PF_RGBA8_Z24_ATTRS,
    PF_RGBAF32_ATTRS,
    PF_RGBAF32_Z24_ATTRS
};
#endif

@interface BooCocoaResponder : NSResponder <NSTextInputClient> {
@public
  NSUInteger lastModifiers;
  boo::GraphicsContextCocoa* booContext;
  NSView* parentView;
  NSTextInputContext* textContext;
}
- (id)initWithBooContext:(boo::GraphicsContextCocoa*)bctx View:(NSView*)view;
@end

namespace boo {

class GraphicsContextCocoa : public IGraphicsContext {
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
                             const CVTimeStamp* inNow,
                             const CVTimeStamp* inOutputTime,
                             CVOptionFlags flagsIn,
                             CVOptionFlags* flagsOut,
                             GraphicsContextCocoa* ctx) {
    std::unique_lock<std::mutex> lk(ctx->m_dlmt);
    ctx->m_dlcv.notify_one();
    return kCVReturnSuccess;
  }

public:
  ~GraphicsContextCocoa() override {
    if (m_dispLink) {
      CVDisplayLinkStop(m_dispLink);
      CVDisplayLinkRelease(m_dispLink);
    }
  }

  std::recursive_mutex m_callbackMutex;
  IWindowCallback* m_callback = nullptr;

  int waitForRetrace() {
    std::unique_lock<std::mutex> lk(m_dlmt);
    m_dlcv.wait(lk);
    return 1;
  }

  virtual BooCocoaResponder* responder() const = 0;
};

#if 0
class GraphicsContextCocoaGL;
#endif

class GraphicsContextCocoaMetal;
}

#if 0
@interface GraphicsContextCocoaGLInternal : NSOpenGLView
{
@public
    BooCocoaResponder* resp;
}
- (id)initWithBooContext:(boo::GraphicsContextCocoaGL*)bctx;
@end
#endif

@interface GraphicsContextCocoaMetalInternal : NSView {
@public
  BooCocoaResponder* resp;
  boo::MetalContext* m_ctx;
  boo::IWindow* m_window;
}
- (id)initWithBooContext:(boo::GraphicsContextCocoaMetal*)bctx;

- (void)reshapeHandler;
@end

namespace boo {
static logvisor::Module Log("boo::WindowCocoa");
#if 0
std::unique_ptr<IGraphicsCommandQueue> _NewGLCommandQueue(IGraphicsContext* parent, GLContext* glCtx);
std::unique_ptr<IGraphicsDataFactory> _NewGLDataFactory(IGraphicsContext* parent, GLContext* glCtx);
#endif

std::unique_ptr<IGraphicsCommandQueue> _NewMetalCommandQueue(MetalContext* ctx, IWindow* parentWindow,
                                                             IGraphicsContext* parent);

std::unique_ptr<IGraphicsDataFactory> _NewMetalDataFactory(IGraphicsContext* parent, MetalContext* ctx);

#if 0
void _CocoaUpdateLastGLCtx(NSOpenGLContext* lastGLCtx);

class GraphicsContextCocoaGL : public GraphicsContextCocoa
{
    GraphicsContextCocoaGLInternal* m_nsContext = nullptr;

    std::unique_ptr<IGraphicsDataFactory> m_dataFactory;
    std::unique_ptr<IGraphicsCommandQueue> m_commandQueue;
    NSOpenGLContext* m_mainCtx = nullptr;
    NSOpenGLContext* m_loadCtx = nullptr;

public:
    NSOpenGLContext* m_lastCtx;
    GLContext* m_glCtx;

    GraphicsContextCocoaGL(EGraphicsAPI api, IWindow* parentWindow,
                           NSOpenGLContext* lastGLCtx, GLContext* glCtx)
    : GraphicsContextCocoa(api, glCtx->m_deepColor ?
      EPixelFormat::RGBA16 : EPixelFormat::RGBA8, parentWindow),
      m_lastCtx(lastGLCtx), m_glCtx(glCtx)
    {
        m_dataFactory = _NewGLDataFactory(this, glCtx);
    }

    ~GraphicsContextCocoaGL() override
    {
        m_commandQueue->stopRenderer();
        fmt::print(FMT_STRING("CONTEXT DESTROYED\n"));
    }

    void _setCallback(IWindowCallback* cb) override
    {
        std::lock_guard<std::recursive_mutex> lk(m_callbackMutex);
        m_callback = cb;
    }

    EGraphicsAPI getAPI() const override
    {
        return m_api;
    }

    EPixelFormat getPixelFormat() const override
    {
        return m_pf;
    }

    void setPixelFormat(EPixelFormat pf) override
    {
        if (pf > EPixelFormat::RGBAF32_Z24)
            return;
        m_pf = pf;
    }

    bool initializeContext(void*) override
    {
        m_nsContext = [[GraphicsContextCocoaGLInternal alloc] initWithBooContext:this];
        if (!m_nsContext)
            Log.report(logvisor::Fatal, FMT_STRING("unable to make new NSOpenGLView"));
        [(__bridge NSWindow*)(void*)m_parentWindow->getPlatformHandle() setContentView:m_nsContext];
        CVDisplayLinkCreateWithActiveCGDisplays(&m_dispLink);
        CVDisplayLinkSetOutputCallback(m_dispLink, (CVDisplayLinkOutputCallback)DLCallback, this);
        CVDisplayLinkStart(m_dispLink);
        m_commandQueue = _NewGLCommandQueue(this, m_glCtx);
        m_commandQueue->startRenderer();
        return true;
    }

    void makeCurrent() override
    {
        [[m_nsContext openGLContext] makeCurrentContext];
    }

    void postInit() override
    {
    }

    IGraphicsCommandQueue* getCommandQueue() override
    {
        return m_commandQueue.get();
    }

    IGraphicsDataFactory* getDataFactory() override
    {
        return m_dataFactory.get();
    }

    IGraphicsDataFactory* getMainContextDataFactory() override
    {
        if (!m_mainCtx)
        {
            NSOpenGLPixelFormat* nspf = [[NSOpenGLPixelFormat alloc] initWithAttributes:PF_TABLE[int(m_pf)]];
            m_mainCtx = [[NSOpenGLContext alloc] initWithFormat:nspf shareContext:[m_nsContext openGLContext]];
            if (!m_mainCtx)
                Log.report(logvisor::Fatal, FMT_STRING("unable to make main NSOpenGLContext"));
        }
        [m_mainCtx makeCurrentContext];
        return m_dataFactory.get();
    }

    IGraphicsDataFactory* getLoadContextDataFactory() override
    {
        if (!m_loadCtx)
        {
            NSOpenGLPixelFormat* nspf = [[NSOpenGLPixelFormat alloc] initWithAttributes:PF_TABLE[int(m_pf)]];
            m_loadCtx = [[NSOpenGLContext alloc] initWithFormat:nspf shareContext:[m_nsContext openGLContext]];
            if (!m_loadCtx)
                Log.report(logvisor::Fatal, FMT_STRING("unable to make load NSOpenGLContext"));
        }
        [m_loadCtx makeCurrentContext];
        return m_dataFactory.get();
    }

    void present() override
    {
        [[m_nsContext openGLContext] flushBuffer];
    }

    BooCocoaResponder* responder() const override
    {
        if (!m_nsContext)
            return nullptr;
        return m_nsContext->resp;
    }

};

IGraphicsContext* _GraphicsContextCocoaGLNew(IGraphicsContext::EGraphicsAPI api,
                                             IWindow* parentWindow, NSOpenGLContext* lastGLCtx,
                                             GLContext* glCtx)
{
    if (api != IGraphicsContext::EGraphicsAPI::OpenGL3_3 && api != IGraphicsContext::EGraphicsAPI::OpenGL4_2)
        return nullptr;

    /* Create temporary context to query GL version */
    NSOpenGLPixelFormat* nspf = [[NSOpenGLPixelFormat alloc] initWithAttributes:PF_RGBA8_ATTRS];
    if (!nspf)
        return nullptr;
    NSOpenGLContext* nsctx = [[NSOpenGLContext alloc] initWithFormat:nspf shareContext:nil];
    if (!nsctx)
        return nullptr;
    [nsctx makeCurrentContext];
    const char* glVersion = (char*)glGetString(GL_VERSION);
    unsigned major = 0;
    unsigned minor = 0;
    if (glVersion)
    {
        major = glVersion[0] - '0';
        minor = glVersion[2] - '0';
    }
    if (glewInit() != GLEW_OK)
        Log.report(logvisor::Fatal, FMT_STRING("glewInit failed"));
    [NSOpenGLContext clearCurrentContext];
    if (!glVersion)
        return nullptr;

    if (major > 4 || (major == 4 && minor >= 2))
        api = IGraphicsContext::EGraphicsAPI::OpenGL4_2;
    else if (major == 3 && minor >= 3)
        if (api == IGraphicsContext::EGraphicsAPI::OpenGL4_2)
            return nullptr;

    return new GraphicsContextCocoaGL(api, parentWindow, lastGLCtx, glCtx);
}
#endif

#if BOO_HAS_METAL

class GraphicsContextCocoaMetal : public GraphicsContextCocoa {
  GraphicsContextCocoaMetalInternal* m_nsContext = nullptr;

  std::unique_ptr<IGraphicsDataFactory> m_dataFactory;
  std::unique_ptr<IGraphicsCommandQueue> m_commandQueue;

public:
  IWindow* m_parentWindow;
  MetalContext* m_metalCtx;

  GraphicsContextCocoaMetal(EGraphicsAPI api, IWindow* parentWindow, MetalContext* metalCtx)
    : GraphicsContextCocoa(api,
                           (metalCtx->m_pixelFormat == MTLPixelFormatRGBA16Float) ?
                           EPixelFormat::RGBA16 : EPixelFormat::RGBA8, parentWindow),
      m_parentWindow(parentWindow), m_metalCtx(metalCtx) {
    m_dataFactory = _NewMetalDataFactory(this, metalCtx);
  }

  ~GraphicsContextCocoaMetal() override {
    m_commandQueue->stopRenderer();
    m_metalCtx->m_windows.erase(m_parentWindow);
  }

  void _setCallback(IWindowCallback* cb) override {
    std::lock_guard<std::recursive_mutex> lk(m_callbackMutex);
    m_callback = cb;
  }

  EGraphicsAPI getAPI() const override {
    return m_api;
  }

  EPixelFormat getPixelFormat() const override {
    return m_pf;
  }

  void setPixelFormat(EPixelFormat pf) override {
    if (pf > EPixelFormat::RGBAF32_Z24)
      return;
    m_pf = pf;
  }

  bool initializeContext(void*) override {
    MetalContext::Window& w = m_metalCtx->m_windows[m_parentWindow];
    m_nsContext = [[GraphicsContextCocoaMetalInternal alloc] initWithBooContext:this];
    if (!m_nsContext)
      Log.report(logvisor::Fatal, FMT_STRING("unable to make new NSView for Metal"));
    w.m_metalLayer = (CAMetalLayer*) m_nsContext.layer;
    [(__bridge NSWindow*) (void*) m_parentWindow->getPlatformHandle() setContentView:m_nsContext];
    CVDisplayLinkCreateWithActiveCGDisplays(&m_dispLink);
    CVDisplayLinkSetOutputCallback(m_dispLink, (CVDisplayLinkOutputCallback) DLCallback, this);
    CVDisplayLinkStart(m_dispLink);
    m_commandQueue = _NewMetalCommandQueue(m_metalCtx, m_parentWindow, this);
    m_commandQueue->startRenderer();
    return true;
  }

  void makeCurrent() override {
  }

  void postInit() override {
  }

  IGraphicsCommandQueue* getCommandQueue() override {
    return m_commandQueue.get();
  }

  IGraphicsDataFactory* getDataFactory() override {
    return m_dataFactory.get();
  }

  IGraphicsDataFactory* getMainContextDataFactory() override {
    return m_dataFactory.get();
  }

  IGraphicsDataFactory* getLoadContextDataFactory() override {
    return m_dataFactory.get();
  }

  void present() override {
  }

  BooCocoaResponder* responder() const override {
    if (!m_nsContext)
      return nullptr;
    return m_nsContext->resp;
  }

};

IGraphicsContext* _GraphicsContextCocoaMetalNew(IGraphicsContext::EGraphicsAPI api,
                                                IWindow* parentWindow,
                                                MetalContext* metalCtx) {
  if (api != IGraphicsContext::EGraphicsAPI::Metal)
    return nullptr;
  return new GraphicsContextCocoaMetal(api, parentWindow, metalCtx);
}

#endif

}

@implementation BooCocoaResponder
- (id)initWithBooContext:(boo::GraphicsContextCocoa*)bctx View:(NSView*)view {
  lastModifiers = 0;
  booContext = bctx;
  parentView = view;
  textContext = [[NSTextInputContext alloc] initWithClient:self];
  return self;
}

- (BOOL)hasMarkedText {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (booContext->m_callback) {
    boo::ITextInputCallback* textCb = booContext->m_callback->getTextInputCallback();
    if (textCb)
      return textCb->hasMarkedText();
  }
  return false;
}

- (NSRange)markedRange {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (booContext->m_callback) {
    boo::ITextInputCallback* textCb = booContext->m_callback->getTextInputCallback();
    if (textCb) {
      std::pair<int, int> rng = textCb->markedRange();
      return NSMakeRange(rng.first < 0 ? NSNotFound : rng.first, rng.second);
    }
  }
  return NSMakeRange(NSNotFound, 0);
}

- (NSRange)selectedRange {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (booContext->m_callback) {
    boo::ITextInputCallback* textCb = booContext->m_callback->getTextInputCallback();
    if (textCb) {
      std::pair<int, int> rng = textCb->selectedRange();
      return NSMakeRange(rng.first < 0 ? NSNotFound : rng.first, rng.second);
    }
  }
  return NSMakeRange(NSNotFound, 0);
}

- (void)setMarkedText:(id)aString selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (booContext->m_callback) {
    boo::ITextInputCallback* textCb = booContext->m_callback->getTextInputCallback();
    if (textCb) {
      NSString* plainStr = aString;
      if ([aString isKindOfClass:[NSAttributedString class]])
        plainStr = ((NSAttributedString*) aString).string;
      textCb->setMarkedText([plainStr UTF8String],
                            std::make_pair(selectedRange.location, selectedRange.length),
                            std::make_pair(replacementRange.location == NSNotFound ? -1 : replacementRange.location,
                                           replacementRange.length));
    }
  }
}

- (void)unmarkText {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (booContext->m_callback) {
    boo::ITextInputCallback* textCb = booContext->m_callback->getTextInputCallback();
    if (textCb)
      textCb->unmarkText();
  }
}

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 101100

- (NSArray<NSString*>*)validAttributesForMarkedText {
  return @[];
}

#else
- (NSArray*)validAttributesForMarkedText
{
    return @[];
}
#endif

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)aRange actualRange:(NSRangePointer)actualRange {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (booContext->m_callback) {
    boo::ITextInputCallback* textCb = booContext->m_callback->getTextInputCallback();
    if (textCb) {
      std::pair<int, int> actualRng;
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

- (void)insertText:(id)aString replacementRange:(NSRange)replacementRange {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (booContext->m_callback) {
    boo::ITextInputCallback* textCb = booContext->m_callback->getTextInputCallback();
    if (textCb) {
      NSString* plainStr = aString;
      if ([aString isKindOfClass:[NSAttributedString class]])
        plainStr = ((NSAttributedString*) aString).string;
      textCb->insertText([plainStr UTF8String],
                         std::make_pair(replacementRange.location == NSNotFound ? -1 : replacementRange.location,
                                        replacementRange.length));
    }
  }
}

- (NSUInteger)characterIndexForPoint:(NSPoint)aPoint {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (booContext->m_callback) {
    boo::ITextInputCallback* textCb = booContext->m_callback->getTextInputCallback();
    if (textCb) {
      NSPoint backingPoint = [parentView convertPointToBacking:aPoint];
      boo::SWindowCoord coord = {{int(backingPoint.x), int(backingPoint.y)},
                                 {int(aPoint.x),       int(aPoint.y)}};
      int idx = textCb->characterIndexAtPoint(coord);
      if (idx < 0)
        return NSNotFound;
      return idx;
    }
  }
  return NSNotFound;
}

- (NSRect)firstRectForCharacterRange:(NSRange)aRange actualRange:(NSRangePointer)actualRange {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (booContext->m_callback) {
    boo::ITextInputCallback* textCb = booContext->m_callback->getTextInputCallback();
    if (textCb) {
      std::pair<int, int> actualRng;
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

- (void)doCommandBySelector:(SEL)aSelector {
}

static inline boo::EModifierKey getMod(NSUInteger flags) {
  boo::EModifierKey ret = boo::EModifierKey::None;
  if (flags & NSEventModifierFlagControl)
    ret |= boo::EModifierKey::Ctrl;
  if (flags & NSEventModifierFlagOption)
    ret |= boo::EModifierKey::Alt;
  if (flags & NSEventModifierFlagShift)
    ret |= boo::EModifierKey::Shift;
  if (flags & NSEventModifierFlagCommand)
    ret |= boo::EModifierKey::Command;
  return ret;
}

static inline boo::EMouseButton getButton(NSEvent* event) {
  NSInteger buttonNumber = event.buttonNumber;
  if (buttonNumber == 3)
    return boo::EMouseButton::Middle;
  else if (buttonNumber == 4)
    return boo::EMouseButton::Aux1;
  else if (buttonNumber == 5)
    return boo::EMouseButton::Aux2;
  return boo::EMouseButton::None;
}

- (void)mouseDown:(NSEvent*)theEvent {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (!booContext->m_callback)
    return;
  NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
  float pixelFactor = [[parentView window] backingScaleFactor];
  NSRect frame = [parentView frame];
  boo::SWindowCoord coord =
    {
      {int(liw.x * pixelFactor),        int(liw.y * pixelFactor)},
      {int(liw.x),                      int(liw.y)},
      {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
    };
  booContext->m_callback->mouseDown(coord, boo::EMouseButton::Primary,
                                    getMod([theEvent modifierFlags]));
  [textContext handleEvent:theEvent];
}

- (void)mouseUp:(NSEvent*)theEvent {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (!booContext->m_callback)
    return;
  NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
  float pixelFactor = [[parentView window] backingScaleFactor];
  NSRect frame = [parentView frame];
  boo::SWindowCoord coord =
    {
      {int(liw.x * pixelFactor),        int(liw.y * pixelFactor)},
      {int(liw.x),                      int(liw.y)},
      {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
    };
  booContext->m_callback->mouseUp(coord, boo::EMouseButton::Primary,
                                  getMod([theEvent modifierFlags]));
  [textContext handleEvent:theEvent];
}

- (void)rightMouseDown:(NSEvent*)theEvent {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (!booContext->m_callback)
    return;
  NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
  float pixelFactor = [[parentView window] backingScaleFactor];
  NSRect frame = [parentView frame];
  boo::SWindowCoord coord =
    {
      {int(liw.x * pixelFactor),        int(liw.y * pixelFactor)},
      {int(liw.x),                      int(liw.y)},
      {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
    };
  booContext->m_callback->mouseDown(coord, boo::EMouseButton::Secondary,
                                    getMod([theEvent modifierFlags]));
  [textContext handleEvent:theEvent];
}

- (void)rightMouseUp:(NSEvent*)theEvent {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (!booContext->m_callback)
    return;
  NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
  float pixelFactor = [[parentView window] backingScaleFactor];
  NSRect frame = [parentView frame];
  boo::SWindowCoord coord =
    {
      {int(liw.x * pixelFactor),        int(liw.y * pixelFactor)},
      {int(liw.x),                      int(liw.y)},
      {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
    };
  booContext->m_callback->mouseUp(coord, boo::EMouseButton::Secondary,
                                  getMod([theEvent modifierFlags]));
  [textContext handleEvent:theEvent];
}

- (void)otherMouseDown:(NSEvent*)theEvent {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
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
      {int(liw.x * pixelFactor),        int(liw.y * pixelFactor)},
      {int(liw.x),                      int(liw.y)},
      {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
    };
  booContext->m_callback->mouseDown(coord, button, getMod([theEvent modifierFlags]));
  [textContext handleEvent:theEvent];
}

- (void)otherMouseUp:(NSEvent*)theEvent {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
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
      {int(liw.x * pixelFactor),        int(liw.y * pixelFactor)},
      {int(liw.x),                      int(liw.y)},
      {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
    };
  booContext->m_callback->mouseUp(coord, button, getMod([theEvent modifierFlags]));
  [textContext handleEvent:theEvent];
}

- (void)mouseMoved:(NSEvent*)theEvent {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (!booContext->m_callback)
    return;
  NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
  if (theEvent.window == [parentView window] && NSPointInRect(liw, parentView.frame)) {
    float pixelFactor = [[parentView window] backingScaleFactor];
    NSRect frame = [parentView frame];
    boo::SWindowCoord coord =
      {
        {int(liw.x * pixelFactor),        int(liw.y * pixelFactor)},
        {int(liw.x),                      int(liw.y)},
        {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
      };
    booContext->m_callback->mouseMove(coord);
  }
  [textContext handleEvent:theEvent];
}

- (void)mouseDragged:(NSEvent*)theEvent {
  [self mouseMoved:theEvent];
}

- (void)rightMouseDragged:(NSEvent*)theEvent {
  [self mouseMoved:theEvent];
}

- (void)otherMouseDragged:(NSEvent*)theEvent {
  [self mouseMoved:theEvent];
}

- (void)mouseEntered:(NSEvent*)theEvent {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (!booContext->m_callback)
    return;
  NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
  float pixelFactor = [[parentView window] backingScaleFactor];
  NSRect frame = [parentView frame];
  boo::SWindowCoord coord =
    {
      {int(liw.x * pixelFactor),        int(liw.y * pixelFactor)},
      {int(liw.x),                      int(liw.y)},
      {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
    };
  booContext->m_callback->mouseEnter(coord);
}

- (void)mouseExited:(NSEvent*)theEvent {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (!booContext->m_callback)
    return;
  NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
  float pixelFactor = [[parentView window] backingScaleFactor];
  NSRect frame = [parentView frame];
  boo::SWindowCoord coord =
    {
      {int(liw.x * pixelFactor),        int(liw.y * pixelFactor)},
      {int(liw.x),                      int(liw.y)},
      {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
    };
  booContext->m_callback->mouseLeave(coord);
}

- (void)scrollWheel:(NSEvent*)theEvent {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (!booContext->m_callback)
    return;
  NSPoint liw = [parentView convertPoint:[theEvent locationInWindow] fromView:nil];
  float pixelFactor = [[parentView window] backingScaleFactor];
  NSRect frame = [parentView frame];
  boo::SWindowCoord coord =
    {
      {int(liw.x * pixelFactor),        int(liw.y * pixelFactor)},
      {int(liw.x),                      int(liw.y)},
      {float(liw.x / frame.size.width), float(liw.y / frame.size.height)}
    };
  boo::SScrollDelta scroll =
    {
      {(float) [theEvent scrollingDeltaX], (float) [theEvent scrollingDeltaY]},
      (bool) [theEvent hasPreciseScrollingDeltas], true
    };
  booContext->m_callback->scroll(coord, scroll);
  [textContext handleEvent:theEvent];
}

- (void)touchesBeganWithEvent:(NSEvent*)event {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (!booContext->m_callback)
    return;
  for (NSTouch* touch in [event touchesMatchingPhase:NSTouchPhaseBegan inView:nil]) {
    NSPoint pos = touch.normalizedPosition;
    boo::STouchCoord coord =
      {
        {(float) pos.x, (float) pos.y}
      };
    booContext->m_callback->touchDown(coord, (uintptr_t) touch.identity);
  }
}

- (void)touchesEndedWithEvent:(NSEvent*)event {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (!booContext->m_callback)
    return;
  for (NSTouch* touch in [event touchesMatchingPhase:NSTouchPhaseEnded inView:nil]) {
    NSPoint pos = touch.normalizedPosition;
    boo::STouchCoord coord =
      {
        {(float) pos.x, (float) pos.y}
      };
    booContext->m_callback->touchUp(coord, (uintptr_t) touch.identity);
  }
}

- (void)touchesMovedWithEvent:(NSEvent*)event {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (!booContext->m_callback)
    return;
  for (NSTouch* touch in [event touchesMatchingPhase:NSTouchPhaseMoved inView:nil]) {
    NSPoint pos = touch.normalizedPosition;
    boo::STouchCoord coord =
      {
        {(float) pos.x, (float) pos.y}
      };
    booContext->m_callback->touchMove(coord, (uintptr_t) touch.identity);
  }
}

- (void)touchesCancelledWithEvent:(NSEvent*)event {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (!booContext->m_callback)
    return;
  for (NSTouch* touch in [event touchesMatchingPhase:NSTouchPhaseCancelled inView:nil]) {
    NSPoint pos = touch.normalizedPosition;
    boo::STouchCoord coord =
      {
        {(float) pos.x, (float) pos.y}
      };
    booContext->m_callback->touchUp(coord, (uintptr_t) touch.identity);
  }
}

/* keycodes for keys that are independent of keyboard layout*/
enum {
  kVK_Return = 0x24,
  kVK_Tab = 0x30,
  kVK_Space = 0x31,
  kVK_Delete = 0x33,
  kVK_Escape = 0x35,
  kVK_Command = 0x37,
  kVK_Shift = 0x38,
  kVK_CapsLock = 0x39,
  kVK_Option = 0x3A,
  kVK_Control = 0x3B,
  kVK_RightShift = 0x3C,
  kVK_RightOption = 0x3D,
  kVK_RightControl = 0x3E,
  kVK_Function = 0x3F,
  kVK_F17 = 0x40,
  kVK_VolumeUp = 0x48,
  kVK_VolumeDown = 0x49,
  kVK_Mute = 0x4A,
  kVK_F18 = 0x4F,
  kVK_F19 = 0x50,
  kVK_F20 = 0x5A,
  kVK_F5 = 0x60,
  kVK_F6 = 0x61,
  kVK_F7 = 0x62,
  kVK_F3 = 0x63,
  kVK_F8 = 0x64,
  kVK_F9 = 0x65,
  kVK_F11 = 0x67,
  kVK_F13 = 0x69,
  kVK_F16 = 0x6A,
  kVK_F14 = 0x6B,
  kVK_F10 = 0x6D,
  kVK_F12 = 0x6F,
  kVK_F15 = 0x71,
  kVK_Help = 0x72,
  kVK_Home = 0x73,
  kVK_PageUp = 0x74,
  kVK_ForwardDelete = 0x75,
  kVK_F4 = 0x76,
  kVK_End = 0x77,
  kVK_F2 = 0x78,
  kVK_PageDown = 0x79,
  kVK_F1 = 0x7A,
  kVK_LeftArrow = 0x7B,
  kVK_RightArrow = 0x7C,
  kVK_DownArrow = 0x7D,
  kVK_UpArrow = 0x7E
};

static boo::ESpecialKey translateKeycode(short code) {
  switch (code) {
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
  case kVK_Tab:
    return boo::ESpecialKey::Tab;
  default:
    return boo::ESpecialKey::None;
  }
}

- (void)keyDown:(NSEvent*)theEvent {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
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

- (void)keyUp:(NSEvent*)theEvent {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
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

- (void)flagsChanged:(NSEvent*)theEvent {
  std::lock_guard<std::recursive_mutex> lk(booContext->m_callbackMutex);
  if (!booContext->m_callback)
    return;
  NSUInteger modFlags = theEvent.modifierFlags;
  if (modFlags != lastModifiers) {
    NSUInteger changedFlags = modFlags ^lastModifiers;

    NSUInteger downFlags = changedFlags & modFlags;
    if (downFlags & NSEventModifierFlagControl)
      booContext->m_callback->modKeyDown(boo::EModifierKey::Ctrl, false);
    if (downFlags & NSEventModifierFlagOption)
      booContext->m_callback->modKeyDown(boo::EModifierKey::Alt, false);
    if (downFlags & NSEventModifierFlagShift)
      booContext->m_callback->modKeyDown(boo::EModifierKey::Shift, false);
    if (downFlags & NSEventModifierFlagCommand)
      booContext->m_callback->modKeyDown(boo::EModifierKey::Command, false);

    NSUInteger upFlags = changedFlags & ~modFlags;
    if (upFlags & NSEventModifierFlagControl)
      booContext->m_callback->modKeyUp(boo::EModifierKey::Ctrl);
    if (upFlags & NSEventModifierFlagOption)
      booContext->m_callback->modKeyUp(boo::EModifierKey::Alt);
    if (upFlags & NSEventModifierFlagShift)
      booContext->m_callback->modKeyUp(boo::EModifierKey::Shift);
    if (upFlags & NSEventModifierFlagCommand)
      booContext->m_callback->modKeyUp(boo::EModifierKey::Command);

    lastModifiers = modFlags;
  }
  [textContext handleEvent:theEvent];
}

- (BOOL)acceptsTouchEvents {
  return YES;
}

- (BOOL)acceptsFirstResponder {
  return YES;
}

@end

#if 0
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
    std::lock_guard<std::recursive_mutex> lk(resp->booContext->m_callbackMutex);
    NSRect frame = [self convertRectToBacking:self.frame];
    boo::SWindowRect rect = {int(frame.origin.x), int(frame.origin.y),
                             int(frame.size.width), int(frame.size.height)};
    if (resp->booContext->m_callback)
        resp->booContext->m_callback->resized(rect, true);
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

- (BOOL)wantsBestResolutionOpenGLSurface
{
    return YES;
}

@end
#endif

#if BOO_HAS_METAL

@implementation GraphicsContextCocoaMetalInternal
- (id)initWithBooContext:(boo::GraphicsContextCocoaMetal*)bctx {
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

- (CALayer*)makeBackingLayer {
  CAMetalLayer* layer = [CAMetalLayer new];
  layer.device = m_ctx->m_dev;
  layer.pixelFormat = m_ctx->m_pixelFormat;
  layer.framebufferOnly = NO;
  return layer;
}

- (BOOL)acceptsTouchEvents {
  return YES;
}

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (NSResponder*)nextResponder {
  return resp;
}

- (void)reshapeHandler {
  std::lock_guard<std::recursive_mutex> lk1(resp->booContext->m_callbackMutex);
  NSRect frame = [self convertRectToBacking:self.frame];
  boo::SWindowRect rect = {int(frame.origin.x), int(frame.origin.y),
                           int(frame.size.width), int(frame.size.height)};
  boo::MetalContext::Window& w = m_ctx->m_windows[m_window];
  std::unique_lock<std::mutex> lk(w.m_resizeLock);
  if (resp->booContext->m_callback)
    resp->booContext->m_callback->resized(rect, false);
  w.m_size = CGSizeMake(rect.size[0], rect.size[1]);
  w.m_needsResize = YES;
}

- (void)setFrameSize:(NSSize)newSize {
  [super setFrameSize:newSize];
  [self reshapeHandler];
}

- (void)setBoundsSize:(NSSize)newSize {
  [super setBoundsSize:newSize];
  [self reshapeHandler];
}

- (void)viewDidChangeBackingProperties {
  [super viewDidChangeBackingProperties];
  [self reshapeHandler];
}

@end

#endif

namespace boo {

static NSString* ClipboardTypes[] =
  {
    0, NSPasteboardTypeString, NSPasteboardTypeString, NSPasteboardTypePNG
  };

class WindowCocoa : public IWindow {
  WindowCocoaInternal* m_nsWindow;
  GraphicsContextCocoa* m_gfxCtx;
  EMouseCursor m_cursor = EMouseCursor::None;

public:

  void setup(std::string_view title, MetalContext* metalCtx) {
    dispatch_sync(dispatch_get_main_queue(),
                  ^{
                    std::shared_ptr<boo::WindowCocoa> windowPtr =
                      std::static_pointer_cast<boo::WindowCocoa>(shared_from_this());
                    m_nsWindow = [[WindowCocoaInternal alloc] initWithBooWindow:windowPtr title:title];
                    m_gfxCtx = static_cast<GraphicsContextCocoa*>(
                      _GraphicsContextCocoaMetalNew(IGraphicsContext::EGraphicsAPI::Metal,
                                                    this, metalCtx));
                    m_gfxCtx->initializeContext(nullptr);
                  });
    m_gfxCtx->getMainContextDataFactory();
  }

  void _clearWindow() {
    m_nsWindow = nullptr;
  }

  ~WindowCocoa() override {
    APP->_deletedWindow(this);
  }

  void setCallback(IWindowCallback* cb) override {
    m_gfxCtx->_setCallback(cb);
  }

  void closeWindow() override {
    dispatch_sync(dispatch_get_main_queue(),
                  ^{
                    [m_nsWindow close];
                  });
  }

  void showWindow() override {
    dispatch_sync(dispatch_get_main_queue(),
                  ^{
                    [m_nsWindow makeKeyAndOrderFront:nil];
                  });
  }

  void hideWindow() override {
    dispatch_sync(dispatch_get_main_queue(),
                  ^{
                    [m_nsWindow orderOut:nil];
                  });
  }

  std::string getTitle() override {
    return [[m_nsWindow title] UTF8String];
  }

  void setTitle(std::string_view title) override {
    dispatch_sync(dispatch_get_main_queue(),
                  ^{
                    [m_nsWindow setTitle:[NSString stringWithUTF8String:title.data()]];
                  });
  }

  void setCursor(EMouseCursor cursor) override {
    if (cursor == m_cursor)
      return;
    m_cursor = cursor;
    dispatch_async(dispatch_get_main_queue(),
                   ^{
                     switch (cursor) {
                     case EMouseCursor::Pointer:
                       [[NSCursor arrowCursor] set];
                       break;
#if !USE_PRIVATE_APIS
                     case EMouseCursor::HorizontalArrow:
                       [[NSCursor resizeLeftRightCursor] set];
                       break;
                     case EMouseCursor::VerticalArrow:
                       [[NSCursor resizeUpDownCursor] set];
                       break;
#endif
                     case EMouseCursor::IBeam:
                       [[NSCursor IBeamCursor] set];
                       break;
                     case EMouseCursor::Crosshairs:
                       [[NSCursor crosshairCursor] set];
                       break;
                     case EMouseCursor::Hand:
                       [[NSCursor pointingHandCursor] set];
                       break;
                     case EMouseCursor::NotAllowed:
                       [[NSCursor operationNotAllowedCursor] set];
                       break;
#if USE_PRIVATE_APIS
                     // Private API cursors
                     case EMouseCursor::HorizontalArrow:
                       [(NSCursor*)[[NSCursor class] performSelector:@selector(_windowResizeEastWestCursor)] set];
                       break;
                     case EMouseCursor::VerticalArrow:
                       [(NSCursor*)[[NSCursor class] performSelector:@selector(_windowResizeNorthSouthCursor)] set];
                       break;
                     case EMouseCursor::BottomRightArrow:
                       [(NSCursor*)[[NSCursor class] performSelector:@selector(_windowResizeNorthWestSouthEastCursor)] set];
                       break;
                     case EMouseCursor::BottomLeftArrow:
                       [(NSCursor*)[[NSCursor class] performSelector:@selector(_windowResizeNorthEastSouthWestCursor)] set];
                       break;
#endif
                     default:
                       break;
                     }
                   });
  }

  void setWaitCursor(bool wait) override {}

  double getWindowRefreshRate() const override {
    /* TODO: Actually get refresh rate */
    return 60.0;
  }

  void setWindowFrameDefault() override {
    dispatch_sync(dispatch_get_main_queue(),
                  ^{
                    NSScreen* mainScreen = [NSScreen mainScreen];
                    NSRect scrFrame = mainScreen.frame;
                    float x_off = scrFrame.size.width / 3.0;
                    float y_off = scrFrame.size.height / 3.0;
                    [m_nsWindow setFrame:NSMakeRect(x_off, y_off, x_off * 2.0, y_off * 2.0) display:NO];
                  });
  }

  void getWindowFrame(float& xOut, float& yOut, float& wOut, float& hOut) const override {
    NSView* view = [m_nsWindow contentView];
    NSRect wFrame = [view convertRectToBacking:view.frame];
    xOut = wFrame.origin.x;
    yOut = wFrame.origin.y;
    wOut = wFrame.size.width;
    hOut = wFrame.size.height;
  }

  void getWindowFrame(int& xOut, int& yOut, int& wOut, int& hOut) const override {
    NSView* view = [m_nsWindow contentView];
    NSRect wFrame = [view convertRectToBacking:view.frame];
    xOut = wFrame.origin.x;
    yOut = wFrame.origin.y;
    wOut = wFrame.size.width;
    hOut = wFrame.size.height;
  }

  void setWindowFrame(float x, float y, float w, float h) override {
    dispatch_sync(dispatch_get_main_queue(),
                  ^{
                    [m_nsWindow setContentSize:NSMakeSize(w, h)];
                    [m_nsWindow setFrameOrigin:NSMakePoint(x, y)];
                  });
  }

  void setWindowFrame(int x, int y, int w, int h) override {
    dispatch_sync(dispatch_get_main_queue(),
                  ^{
                    [m_nsWindow setContentSize:NSMakeSize(w, h)];
                    [m_nsWindow setFrameOrigin:NSMakePoint(x, y)];
                  });
  }

  float getVirtualPixelFactor() const override {
    return [m_nsWindow backingScaleFactor];
  }

  bool isFullscreen() const override {
    return ([m_nsWindow styleMask] & NSWindowStyleMaskFullScreen) == NSWindowStyleMaskFullScreen;
  }

  void setFullscreen(bool fs) override {
    if ((fs && !isFullscreen()) || (!fs && isFullscreen()))
      dispatch_sync(dispatch_get_main_queue(),
                    ^{
                      [m_nsWindow toggleFullScreen:nil];
                    });
  }

  void claimKeyboardFocus(const int coord[2]) override {
    BooCocoaResponder* resp = m_gfxCtx->responder();
    if (resp) {
      dispatch_async(dispatch_get_main_queue(),
                     ^{
                       if (coord)
                         [resp->textContext activate];
                       else
                         [resp->textContext deactivate];
                     });
    }
  }

  bool clipboardCopy(EClipboardType type, const uint8_t* data, size_t sz) override {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    NSData* d = [NSData dataWithBytes:data length:sz];
    [pb setData:d forType:ClipboardTypes[int(type)]];
    return true;
  }

  std::unique_ptr<uint8_t[]> clipboardPaste(EClipboardType type, size_t& sz) override {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    NSData* d = [pb dataForType:ClipboardTypes[int(type)]];
    if (!d)
      return std::unique_ptr<uint8_t[]>();
    sz = [d length];
    std::unique_ptr<uint8_t[]> ret(new uint8_t[sz]);
    [d getBytes:ret.get() length:sz];
    return ret;
  }

  ETouchType getTouchType() const override {
    return ETouchType::Trackpad;
  }

  void setStyle(EWindowStyle style) override {
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 101000
    if ((style & EWindowStyle::Titlebar) != EWindowStyle::None)
      m_nsWindow.titleVisibility = NSWindowTitleVisible;
    else
      m_nsWindow.titleVisibility = NSWindowTitleHidden;
#endif

    if ((style & EWindowStyle::Close) != EWindowStyle::None)
      m_nsWindow.styleMask |= NSWindowStyleMaskClosable;
    else
      m_nsWindow.styleMask &= ~NSWindowStyleMaskClosable;

    if ((style & EWindowStyle::Resize) != EWindowStyle::None)
      m_nsWindow.styleMask |= NSWindowStyleMaskResizable;
    else
      m_nsWindow.styleMask &= ~NSWindowStyleMaskResizable;
  }

  EWindowStyle getStyle() const override {
    EWindowStyle retval = EWindowStyle::None;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 101000
    retval |= m_nsWindow.titleVisibility == NSWindowTitleVisible ? EWindowStyle::Titlebar : EWindowStyle::None;
#else
    retval |= EWindowStyle::Titlebar;
#endif
    retval |= (m_nsWindow.styleMask & NSWindowStyleMaskClosable) ? EWindowStyle::Close : EWindowStyle::None;
    retval |= (m_nsWindow.styleMask & NSWindowStyleMaskResizable) ? EWindowStyle::Resize : EWindowStyle::None;
    return retval;
  }

  void setTouchBarProvider(void* provider) override {
    dispatch_sync(dispatch_get_main_queue(),
                  ^{
                    [m_nsWindow setTouchBarProvider:(__bridge_transfer id) provider];
                  });
  }

  int waitForRetrace() override {
    return static_cast<GraphicsContextCocoa*>(m_gfxCtx)->waitForRetrace();
  }

  uintptr_t getPlatformHandle() const override {
    return (uintptr_t) m_nsWindow;
  }

  IGraphicsCommandQueue* getCommandQueue() override {
    return m_gfxCtx->getCommandQueue();
  }

  IGraphicsDataFactory* getDataFactory() override {
    return m_gfxCtx->getDataFactory();
  }

  IGraphicsDataFactory* getMainContextDataFactory() override {
    return m_gfxCtx->getMainContextDataFactory();
  }

  IGraphicsDataFactory* getLoadContextDataFactory() override {
    return m_gfxCtx->getLoadContextDataFactory();
  }

};

std::shared_ptr<IWindow> _WindowCocoaNew(SystemStringView title, MetalContext* metalCtx) {
  auto ret = std::make_shared<WindowCocoa>();
  ret->setup(title, metalCtx);
  return ret;
}

}

@implementation WindowCocoaInternal
- (id)initWithBooWindow:(std::shared_ptr<boo::WindowCocoa>&)bw title:(std::string_view)title {
  self = [self initWithContentRect:[self genFrameDefault]
                         styleMask:NSWindowStyleMaskTitled |
                                   NSWindowStyleMaskClosable |
                                   NSWindowStyleMaskMiniaturizable |
                                   NSWindowStyleMaskResizable
                           backing:NSBackingStoreBuffered
                             defer:YES];
  self.delegate = self;
  self.releasedWhenClosed = NO;
  self.title = [NSString stringWithUTF8String:title.data()];

  NSString* titleImgPath = [[NSBundle mainBundle] pathForResource:@"mainicon" ofType:@"icns"];
  NSImage* titleImg = [[NSImage alloc] initByReferencingFile:titleImgPath];
  [self setRepresentedURL:[NSURL URLWithString:@""]];
  NSButton* iconButton = [self standardWindowButton:NSWindowDocumentIconButton];
  [iconButton setImage:titleImg];

  booWindow = bw;
  return self;
}

- (BOOL)window:(NSWindow*)window shouldPopUpDocumentPathMenu:(NSMenu*)menu {
  return NO;
}

- (void)setFrameDefault {
  [self setFrame:[self genFrameDefault] display:NO];
}

- (NSRect)genFrameDefault {
  NSScreen* mainScreen = [NSScreen mainScreen];
  NSRect scrFrame = mainScreen.frame;
  float width = scrFrame.size.width * 2.0 / 3.0;
  float height = scrFrame.size.height * 2.0 / 3.0;
  return NSMakeRect((scrFrame.size.width - width) / 2.0,
                    (scrFrame.size.height - height) / 2.0,
                    width, height);
}

- (void)setTouchBarProvider:(id <NSTouchBarProvider>)provider {
  touchBarProvider = provider;
  self.touchBar = nil;
}

- (NSTouchBar*)makeTouchBar {
  return [touchBarProvider makeTouchBar];
}

- (void)close {
  [super close];
  booWindow->_clearWindow();
}

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (BOOL)acceptsMouseMovedEvents {
  return YES;
}

- (NSWindowCollectionBehavior)collectionBehavior {
  return NSWindowCollectionBehaviorFullScreenPrimary;
}
@end

