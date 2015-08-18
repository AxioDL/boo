#include <AppKit/AppKit.h>

#include "IApplication.hpp"

@interface AppDelegate : NSObject <NSApplicationDelegate>
{
    boo::IApplicationCallback* callback;
    @public
    NSPanel* aboutPanel;
}
- (id)initWithCallback:(boo::IApplicationCallback*)cb;
@end

@implementation AppDelegate
- (id)initWithCallback:(boo::IApplicationCallback*)cb
{
    self = [super init];
    callback = cb;
    return self;
}
- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    (void)notification;
    callback->appLaunched(boo::IApplicationInstance());
}
- (void)applicationWillTerminate:(NSNotification*)notification
{
    (void)notification;
    callback->appQuitting(boo::IApplicationInstance());
}
- (BOOL)application:(NSApplication*)sender openFile:(NSString*)filename
{
    (void)sender;
    return callback->appFileOpen(boo::IApplicationInstance(), [filename UTF8String]);
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
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

namespace boo
{
    
IWindow* _CWindowCocoaNew(const std::string& title);
    
class CApplicationCocoa final : public IApplication
{
    IApplicationCallback& m_callback;
    const std::string m_friendlyName;
    const std::string m_pname;
    const std::vector<std::string> m_args;
    
    NSPanel* aboutPanel;
    
    void _deletedWindow(IWindow* window)
    {
        (void)window;
    }

public:
    CApplicationCocoa(IApplicationCallback& callback,
                      const std::string& friendlyName,
                      const std::string& pname,
                      const std::vector<std::string>& args)
    : m_callback(callback),
      m_friendlyName(friendlyName),
      m_pname(pname),
      m_args(args)
    {}
    
    EPlatformType getPlatformType() const
    {
        return PLAT_COCOA;
    }
    
    void run()
    {
        @autoreleasepool
        {
            NSApplication* app = [NSApplication sharedApplication];
            [app setActivationPolicy:NSApplicationActivationPolicyRegular];
            
            /* Delegate (OS X callbacks) */
            AppDelegate* appDelegate = [[AppDelegate alloc] initWithCallback:&m_callback];
            [app setDelegate:appDelegate];
            
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
            [aboutText setString:@"\nRWK Authors\n\nJackoalan\nAntidote\n"];
            [aboutPanel setContentView:aboutText];
            appDelegate->aboutPanel = aboutPanel;
            
            [app run];
        }
    }
    
    void quit()
    {
        [NSApp terminate:nil];
    }
    
    const std::string& getProcessName() const
    {
        return m_pname;
    }
    
    const std::vector<std::string>& getArgs() const
    {
        return m_args;
    }
    
    IWindow* newWindow(const std::string& title)
    {
        return _CWindowCocoaNew(title);
    }
};

IApplication* APP = NULL;
IApplication* IApplicationBootstrap(IApplication::EPlatformType platform,
                                    IApplicationCallback& cb,
                                    const std::string& friendlyName,
                                    const std::string& pname,
                                    const std::vector<std::string>& args)
{
    if (!APP)
    {
        if (platform != IApplication::PLAT_COCOA &&
            platform != IApplication::PLAT_AUTO)
            return NULL;
        APP = new CApplicationCocoa(cb, friendlyName, pname, args);
    }
    return APP;
}
    
}
