#include <AppKit/AppKit.h>
#include <thread>

#include "boo/IApplication.hpp"
#include "boo/graphicsdev/Metal.hpp"
#include "CocoaCommon.hpp"

#include "logvisor/logvisor.hpp"

#if !__has_feature(objc_arc)
#error ARC Required
#endif

/* If set, application will terminate once client thread reaches end;
 * main() will not get back control. Otherwise, main will get back control
 * but App will not terminate in the normal Cocoa manner (possibly resulting
 * in CoreAnimation warnings). */
#define COCOA_TERMINATE 1

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
static logvisor::Module Log("boo::ApplicationCocoa");

std::shared_ptr<IWindow> _WindowCocoaNew(const SystemString& title, NSOpenGLContext* lastGLCtx,
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
    std::unordered_map<uintptr_t, std::weak_ptr<IWindow>> m_windows;

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
        [fsItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
        [appMenu addItem:[NSMenuItem separatorItem]];
        NSMenuItem* quitItem = [appMenu addItemWithTitle:[NSString stringWithFormat:@"Quit %s", m_friendlyName.c_str()]
                                                  action:@selector(quitApp:)
                                           keyEquivalent:@"q"];
        [quitItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
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
                Log.report(logvisor::Info, "using Metal renderer");
                break;
            }
        if (!m_metalCtx.m_dev)
            Log.report(logvisor::Info, "using OpenGL renderer");
#else
        Log.report(logvisor::Info, "using OpenGL renderer");
#endif
    }

    EPlatformType getPlatformType() const
    {
        return EPlatformType::Cocoa;
    }

    std::thread m_clientThread;
    int m_clientReturn = 0;
    bool m_terminateNow = false;
    int run()
    {
        /* Spawn client thread */
        m_clientThread = std::thread([&]()
        {
            std::string thrName = getFriendlyName() + " Client Thread";
            logvisor::RegisterThreadName(thrName.c_str());

            /* Run app */
            m_clientReturn = m_callback.appMain(this);

            /* Cleanup */
            for (auto& w : m_windows)
                if (std::shared_ptr<IWindow> window = w.second.lock())
                    window->closeWindow();

#if COCOA_TERMINATE
            /* Continue termination */
            dispatch_sync(dispatch_get_main_queue(),
            ^{
                /* Ends modal run loop and continues Cocoa termination */
                [[NSApplication sharedApplication] replyToApplicationShouldTerminate:YES];

                /* If this is reached, application didn't spawn any windows
                 * and must be explicitly terminated */
                m_terminateNow = true;
                [[NSApplication sharedApplication] terminate:nil];
            });
#else
            /* Return control to main() */
            dispatch_sync(dispatch_get_main_queue(),
            ^{
                [[NSApplication sharedApplication] stop:nil];
            });
#endif
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

    std::shared_ptr<IWindow> newWindow(const std::string& title, uint32_t sampleCount)
    {
        auto newWindow = _WindowCocoaNew(title, m_lastGLCtx, &m_metalCtx, sampleCount);
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
    std::string thrName = friendlyName + " Main Thread";
    logvisor::RegisterThreadName(thrName.c_str());
    @autoreleasepool
    {
        if (!APP)
        {
            if (platform != IApplication::EPlatformType::Cocoa &&
                platform != IApplication::EPlatformType::Auto)
                return 1;
            /* Never deallocated to ensure window destructors have access */
            APP = new ApplicationCocoa(cb, uniqueName, friendlyName, pname, args);
        }
        [[NSApplication sharedApplication] run];
        ApplicationCocoa* appCocoa = static_cast<ApplicationCocoa*>(APP);
        if (appCocoa->m_clientThread.joinable())
            appCocoa->m_clientThread.join();
        return appCocoa->m_clientReturn;
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
#if COCOA_TERMINATE
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)app
{
    (void)app;
    if (m_app->m_terminateNow)
        return NSTerminateNow;
    m_app->m_callback.appQuitting(m_app);
    return NSTerminateLater;
}
#else
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)app
{
    (void)app;
    m_app->m_callback.appQuitting(m_app);
    return NSTerminateCancel;
}
#endif
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

