#ifndef IAPPLICATION_HPP
#define IAPPLICATION_HPP

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
    virtual void appMain(IApplication*) {}
    virtual void appQuitting(IApplication*) {}
    virtual void appFilesOpen(IApplication*, const std::vector<SystemString>&) {}
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
    
    virtual void pump()=0;
    virtual const SystemString& getUniqueName() const=0;
    virtual const SystemString& getFriendlyName() const=0;
    virtual const SystemString& getProcessName() const=0;
    virtual const std::vector<SystemString>& getArgs() const=0;
    
    /* Constructors/initializers for sub-objects */
    virtual IWindow* newWindow(const SystemString& title)=0;
    
};

std::unique_ptr<IApplication>
ApplicationBootstrap(IApplication::EPlatformType platform,
                     IApplicationCallback& cb,
                     const SystemString& uniqueName,
                     const SystemString& friendlyName,
                     const SystemString& pname,
                     const std::vector<SystemString>& args,
                     bool singleInstance=true);
extern IApplication* APP;
    
static inline std::unique_ptr<IApplication>
ApplicationBootstrap(IApplication::EPlatformType platform,
                     IApplicationCallback& cb,
                     const SystemString& uniqueName,
                     const SystemString& friendlyName,
                     int argc, const SystemChar** argv,
                     bool singleInstance=true)
{
    if (APP)
        return std::unique_ptr<IApplication>();
    std::vector<SystemString> args;
    for (int i=1 ; i<argc ; ++i)
        args.push_back(argv[i]);
    return ApplicationBootstrap(platform, cb, uniqueName, friendlyName, argv[0], args, singleInstance);
}
    
}

#endif // IAPPLICATION_HPP
