#include <AppKit/AppKit.h>
#include <thread>

#include "boo/IApplication.hpp"
#include "boo/graphicsdev/Metal.hpp"
#include "CocoaCommon.hpp"

#include <LogVisor/LogVisor.hpp>

#if !__has_feature(objc_arc)
#error ARC Required
#endif

namespace boo {class ApplicationCocoa;}
@interface AppDelegate : NSObject <NSApplicationDelegate>
{
    boo::ApplicationCocoa* m_app;
    @public
}
- (id)initWithApp:(boo::ApplicationCocoa*)app;
@end

namespace boo
{
static LogVisor::LogModule Log("boo::ApplicationCocoa");

IWindow* _WindowCocoaNew(const SystemString& title, NSOpenGLContext* lastGLCtx,
                         MetalContext* metalCtx, uint32_t sampleCount);

class ApplicationCocoa : public IApplication
{
public:
    IApplicationCallback& m_callback;
    AppDelegate* m_appDelegate;
private:
    const SystemString m_uniqueName;
    const SystemString m_friendlyName;
    const SystemString m_pname;
    const std::vector<SystemString> m_args;

    NSPanel* aboutPanel;

    /* All windows */
    std::unordered_map<uintptr_t, IWindow*> m_windows;

    MetalContext m_metalCtx;

    void _deletedWindow(IWindow* window)
    {
        m_windows.erase(window->getPlatformHandle());
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
        [[NSApplication sharedApplication] setActivationPolicy:NSApplicationActivationPolicyRegular];

        /* Delegate (OS X callbacks) */
        m_appDelegate = [[AppDelegate alloc] initWithApp:this];
        [[NSApplication sharedApplication] setDelegate:m_appDelegate];

        /* App menu */
        NSMenu* rootMenu = [[NSMenu alloc] initWithTitle:@"main"];
        NSMenu* appMenu = [[NSMenu alloc] initWithTitle:[NSString stringWithUTF8String:m_friendlyName.c_str()]];
        NSMenuItem* fsItem = [appMenu addItemWithTitle:@"Toggle Full Screen"
                                                action:@selector(toggleFs:)
                                         keyEquivalent:@"f"];
        [fsItem setKeyEquivalentModifierMask:NSCommandKeyMask];
        [appMenu addItem:[NSMenuItem separatorItem]];
        NSMenuItem* quitItem = [appMenu addItemWithTitle:[NSString stringWithFormat:@"Quit %s", m_friendlyName.c_str()]
                                                  action:@selector(quitApp:)
                                           keyEquivalent:@"q"];
        [quitItem setKeyEquivalentModifierMask:NSCommandKeyMask];
        [[rootMenu addItemWithTitle:[NSString stringWithUTF8String:m_friendlyName.c_str()]
                            action:nil keyEquivalent:@""] setSubmenu:appMenu];
        [[NSApplication sharedApplication] setMainMenu:rootMenu];

        /* Determine which graphics API to use */
#if BOO_HAS_METAL
        for (const SystemString& arg : args)
            if (!arg.compare("--metal"))
            {
                m_metalCtx.m_dev = MTLCreateSystemDefaultDevice();
                m_metalCtx.m_q = [m_metalCtx.m_dev newCommandQueue];
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
        return EPlatformType::Cocoa;
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
            std::vector<std::unique_ptr<IWindow>> toDelete;
            toDelete.reserve(m_windows.size());
            for (auto& window : m_windows)
                toDelete.emplace_back(window.second);
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

    IWindow* newWindow(const std::string& title, uint32_t sampleCount)
    {
        IWindow* newWindow = _WindowCocoaNew(title, m_lastGLCtx, &m_metalCtx, sampleCount);
        m_windows[newWindow->getPlatformHandle()] = newWindow;
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
            if (platform != IApplication::EPlatformType::Cocoa &&
                platform != IApplication::EPlatformType::Auto)
                return 1;
            APP = new ApplicationCocoa(cb, uniqueName, friendlyName, pname, args);
        }
        [[NSApplication sharedApplication] run];
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

