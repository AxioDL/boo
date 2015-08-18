#ifndef IRUNLOOP_HPP
#define IRUNLOOP_HPP

#include <memory>
#include <string>
#include <vector>

#include "IWindow.hpp"
#include "inputdev/DeviceFinder.hpp"

namespace boo
{
class IApplication;

struct IApplicationCallback
{
    virtual void appLaunched(IApplication* app) {(void)app;}
    virtual void appQuitting(IApplication* app) {(void)app;}
    virtual void appFilesOpen(IApplication* app, const std::vector<std::string>& paths) {(void)app;(void)paths;}
};

class IApplication
{
    friend class WindowCocoa;
    friend class WindowWayland;
    friend class WindowXCB;
    friend class WindowWin32;
    virtual void _deletedWindow(IWindow* window)=0;
public:
    virtual ~IApplication() {}
    
    enum EPlatformType
    {
        PLAT_AUTO        = 0,
        PLAT_WAYLAND     = 1,
        PLAT_XCB         = 2,
        PLAT_ANDROID     = 3,
        PLAT_COCOA       = 4,
        PLAT_COCOA_TOUCH = 5,
        PLAT_WIN32       = 6,
        PLAT_WINRT       = 7,
        PLAT_REVOLUTION  = 8,
        PLAT_CAFE        = 9
    };
    virtual EPlatformType getPlatformType() const=0;
    
    virtual void run()=0;
    virtual void quit()=0;
    virtual const std::string& getUniqueName() const=0;
    virtual const std::string& getFriendlyName() const=0;
    virtual const std::string& getProcessName() const=0;
    virtual const std::vector<std::string>& getArgs() const=0;
    
    /* Constructors/initializers for sub-objects */
    virtual IWindow* newWindow(const std::string& title)=0;
    
};

std::shared_ptr<IApplication>
ApplicationBootstrap(IApplication::EPlatformType platform,
                     IApplicationCallback& cb,
                     const std::string& uniqueName,
                     const std::string& friendlyName,
                     const std::string& pname,
                     const std::vector<std::string>& args,
                     bool singleInstance=true);
extern IApplication* APP;
    
static inline std::shared_ptr<IApplication>
ApplicationBootstrap(IApplication::EPlatformType platform,
                     IApplicationCallback& cb,
                     const std::string& uniqueName,
                     const std::string& friendlyName,
                     int argc, const char** argv,
                     bool singleInstance=true)
{
    if (APP)
        return std::shared_ptr<IApplication>(APP);
    std::vector<std::string> args;
    for (int i=1 ; i<argc ; ++i)
        args.push_back(argv[i]);
    return ApplicationBootstrap(platform, cb, uniqueName, friendlyName, argv[0], args, singleInstance);
}
    
}

#endif // IRUNLOOP_HPP
