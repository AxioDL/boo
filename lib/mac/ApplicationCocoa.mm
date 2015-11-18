#include <AppKit/AppKit.h>
#include <thread>

#include "boo/IApplication.hpp"
#include "boo/graphicsdev/Metal.hpp"
#include "CocoaCommon.hpp"

#include <LogVisor/LogVisor.hpp>

namespace boo {class ApplicationCocoa;}
@interface AppDelegate : NSObject <NSApplicationDelegate>
{
    boo::ApplicationCocoa* m_app;
    @public
    NSPanel* aboutPanel;
}
- (id)initWithApp:(boo::ApplicationCocoa*)app;
@end

namespace boo
{
static LogVisor::LogModule Log("boo::ApplicationCocoa");
    
IWindow* _WindowCocoaNew(const SystemString& title, NSOpenGLContext* lastGLCtx, MetalContext* metalCtx);
    
class ApplicationCocoa : public IApplication
{
public:
    NSApplication* m_app = nullptr;
    IApplicationCallback& m_callback;
private:
    const SystemString m_uniqueName;
    const SystemString m_friendlyName;
    const SystemString m_pname;
    const std::vector<SystemString> m_args;
    
    NSPanel* aboutPanel;
    
    /* All windows */
    std::unordered_map<NSWindow*, IWindow*> m_windows;
    
    MetalContext m_metalCtx;
    
    void _deletedWindow(IWindow* window)
    {
        m_windows.erase((NSWindow*)window->getPlatformHandle());
    }

public:
    ApplicationCocoa(IApplicationCallback& callback,
                     const SystemString& uniqueName,
                     const SystemString& friendlyName,
                     const SystemString& pname,
                     const std::vector<SystemString>& args)
    : m_callback(callback),
      m_uniqueName(uniqueName),
      m_friendlyName(friendlyName),
      m_pname(pname),
      m_args(args)
    {
        m_app = [NSApplication sharedApplication];
        [m_app setActivationPolicy:NSApplicationActivationPolicyRegular];
        
        /* Delegate (OS X callbacks) */
        AppDelegate* appDelegate = [[AppDelegate alloc] initWithApp:this];
        [m_app setDelegate:appDelegate];
        
        /* App menu */
        NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"main"];
        NSMenu* rwkMenu = [[NSMenu alloc] initWithTitle:[[NSString stringWithUTF8String:m_friendlyName.c_str()] autorelease]];
        [rwkMenu addItemWithTitle:[[NSString stringWithFormat:@"About %s", m_friendlyName.c_str()] autorelease]
                           action:@selector(aboutApp:)
                    keyEquivalent:@""];
        NSMenuItem* fsItem = [rwkMenu addItemWithTitle:@"Toggle Full Screen"
                                                action:@selector(toggleFs:)
                                         keyEquivalent:@"f"];
        [fsItem setKeyEquivalentModifierMask:NSCommandKeyMask];
        [rwkMenu addItem:[NSMenuItem separatorItem]];
        NSMenuItem* quit_item = [rwkMenu addItemWithTitle:[[NSString stringWithFormat:@"Quit %s", m_friendlyName.c_str()] autorelease]
                                                   action:@selector(quitApp:)
                                            keyEquivalent:@"q"];
        [quit_item setKeyEquivalentModifierMask:NSCommandKeyMask];
        [[appMenu addItemWithTitle:[[NSString stringWithUTF8String:m_friendlyName.c_str()] autorelease]
                            action:nil keyEquivalent:@""] setSubmenu:rwkMenu];
        [[NSApplication sharedApplication] setMainMenu:appMenu];
        
        /* About panel */
        NSRect aboutCr = NSMakeRect(0, 0, 300, 220);
        aboutPanel = [[NSPanel alloc] initWithContentRect:aboutCr
                                                styleMask:NSUtilityWindowMask|NSTitledWindowMask|NSClosableWindowMask
                                                  backing:NSBackingStoreBuffered defer:YES];
        [aboutPanel setTitle:[[NSString stringWithFormat:@"About %s", m_friendlyName.c_str()] autorelease]];
        NSText* aboutText = [[NSText alloc] initWithFrame:aboutCr];
        [aboutText setEditable:NO];
        [aboutText setAlignment:NSCenterTextAlignment];
        [aboutText setString:@"\nBoo Authors\n\nJackoalan\nAntidote\n"];
        [aboutPanel setContentView:aboutText];
        appDelegate->aboutPanel = aboutPanel;
        
        /* Determine which graphics API to use */
#if BOO_HAS_METAL
        for (const SystemString& arg : args)
            if (!arg.compare("--metal"))
            {
                m_metalCtx.m_dev = MTLCreateSystemDefaultDevice();
                m_metalCtx.m_q = [m_metalCtx.m_dev.get() newCommandQueue];
                Log.report(LogVisor::Info, "using Metal renderer");
                break;
            }
        if (!m_metalCtx.m_dev)
            Log.report(LogVisor::Info, "using OpenGL renderer");
#else
        Log.report(LogVisor::Info, "using OpenGL renderer");
#endif
    }
    
    EPlatformType getPlatformType() const
    {
        return PLAT_COCOA;
    }
    
    std::thread m_clientThread;
    int m_clientReturn = 0;
    int run()
    {
        /* Spawn client thread */
        m_clientThread = std::thread([&]()
        {
            /* Run app */
            m_clientReturn = m_callback.appMain(this);
            
            /* Cleanup here */
            for (auto& window : m_windows)
                delete window.second;
        });
        
        /* Already in Cocoa's event loop; return now */
        return 0;
    }
    
    void quit()
    {
        [NSApp terminate:nil];
    }
    
    const SystemString& getUniqueName() const
    {
        return m_uniqueName;
    }
    
    const SystemString& getFriendlyName() const
    {
        return m_friendlyName;
    }
    
    const SystemString& getProcessName() const
    {
        return m_pname;
    }
    
    const std::vector<SystemString>& getArgs() const
    {
        return m_args;
    }
    
    IWindow* newWindow(const std::string& title)
    {
        IWindow* newWindow = _WindowCocoaNew(title, m_lastGLCtx, &m_metalCtx);
        m_windows[(NSWindow*)newWindow->getPlatformHandle()] = newWindow;
        return newWindow;
    }
    
    /* Last GL context */
    NSOpenGLContext* m_lastGLCtx = nullptr;
};
    
void _CocoaUpdateLastGLCtx(NSOpenGLContext* lastGLCtx)
{
    static_cast<ApplicationCocoa*>(APP)->m_lastGLCtx = lastGLCtx;
}

IApplication* APP = nullptr;
int ApplicationRun(IApplication::EPlatformType platform,
                   IApplicationCallback& cb,
                   const SystemString& uniqueName,
                   const SystemString& friendlyName,
                   const SystemString& pname,
                   const std::vector<SystemString>& args,
                   bool singleInstance)
{
    @autoreleasepool
    {
        if (!APP)
        {
            if (platform != IApplication::PLAT_COCOA &&
                platform != IApplication::PLAT_AUTO)
                return 1;
            APP = new ApplicationCocoa(cb, uniqueName, friendlyName, pname, args);
        }
        [static_cast<ApplicationCocoa*>(APP)->m_app run];
        return static_cast<ApplicationCocoa*>(APP)->m_clientReturn;
    }
}
    
}

@implementation AppDelegate
- (id)initWithApp:(boo::ApplicationCocoa*)app
{
    self = [super init];
    m_app = app;
    return self;
}
- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    (void)notification;
    m_app->run();
}
- (void)applicationWillTerminate:(NSNotification*)notification
{
    (void)notification;
    m_app->m_callback.appQuitting(m_app);
    m_app->m_clientThread.join();
}
- (BOOL)application:(NSApplication*)sender openFile:(NSString*)filename
{
    std::vector<boo::SystemString> strVec;
    strVec.push_back(boo::SystemString([filename UTF8String]));
    m_app->m_callback.appFilesOpen(boo::APP, strVec);
    return true;
}
- (void)application:(NSApplication*)sender openFiles:(NSArray*)filenames
{
    std::vector<boo::SystemString> strVec;
    strVec.reserve([filenames count]);
    for (NSString* str in filenames)
        strVec.push_back(boo::SystemString([str UTF8String]));
    m_app->m_callback.appFilesOpen(boo::APP, strVec);
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
{
    (void)sender;
    return YES;
}
- (IBAction)aboutApp:(id)sender
{
    (void)sender;
    NSRect screenFrame = [[aboutPanel screen] frame];
    CGFloat xPos = NSWidth(screenFrame)/2 - 300/2;
    CGFloat yPos = NSHeight(screenFrame)/2 - 220/2;
    NSRect aboutCr = NSMakeRect(xPos, yPos, 300, 220);
    [aboutPanel setFrame:aboutCr display:NO];
    [aboutPanel makeKeyAndOrderFront:self];
}
- (IBAction)toggleFs:(id)sender
{
    (void)sender;
    [[NSApp keyWindow] toggleFullScreen:nil];
}
- (IBAction)quitApp:(id)sender
{
    (void)sender;
    [NSApp terminate:nil];
}
@end

