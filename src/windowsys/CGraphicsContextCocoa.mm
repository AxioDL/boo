#import <AppKit/AppKit.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include "windowsys/IGraphicsContext.hpp"

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
- (id)initWithBooContext:(boo::CGraphicsContextCocoa*)bctx;
@end

namespace boo
{
    
class CGraphicsContextCocoa final : public IGraphicsContext
{
    
    EGraphicsAPI m_api;
    EPixelFormat m_pf;
    NSWindow* m_parentWindow;
    CGraphicsContextCocoaInternal* m_nsContext;
    NSOpenGLContext* m_nsShareContext;
    
public:
    CGraphicsContextCocoa(EGraphicsAPI api)
    : m_api(api),
      m_pf(PF_RGBA8),
      m_parentWindow(NULL),
      m_nsContext(NULL),
      m_nsShareContext(NULL)
    {}
    
    ~CGraphicsContextCocoa()
    {
        [m_nsContext release];
        [m_nsShareContext release];
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
        m_parentWindow = (NSWindow*)handle;
    }
    
    void initializeContext()
    {
        if (m_nsShareContext)
            return;
        m_nsContext = [[CGraphicsContextCocoaInternal alloc] initWithBooContext:this];
        [m_parentWindow setContentView:m_nsContext];
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
        CGraphicsContextCocoa* newCtx = new CGraphicsContextCocoa(m_api);
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
    
IGraphicsContext* IGraphicsContextNew(IGraphicsContext::EGraphicsAPI api)
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

    return new CGraphicsContextCocoa(api);
}

}

@implementation CGraphicsContextCocoaInternal
- (id)initWithBooContext:(boo::CGraphicsContextCocoa*)bctx
{
    boo::IGraphicsContext::EPixelFormat pf = bctx->getPixelFormat();
    NSOpenGLPixelFormat* nspf = [[NSOpenGLPixelFormat alloc] initWithAttributes:PF_TABLE[pf]];
    self = [self initWithFrame:NSMakeRect(0, 0, 100, 100) pixelFormat:nspf];
    [nspf release];
    return self;
}
@end

