#ifndef IRUNLOOP_HPP
#define IRUNLOOP_HPP

#include <string>
#include <vector>

#include "windowsys/IWindow.hpp"
#include "inputdev/CDeviceFinder.hpp"

namespace boo
{
class IApplication;

struct IApplicationCallback
{
    virtual void appLaunched(IApplication* app) {(void)app;}
    virtual void appQuitting(IApplication* app) {(void)app;}
    virtual bool appFileOpen(IApplication* app, const std::string& path) {(void)app;(void)path;return true;}
};

class IApplication
{
    friend class CWindowCocoa;
    friend class CWindowWayland;
    friend class CWindowXCB;
    friend class CWindowWin32;
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
    virtual const std::string& getProcessName() const=0;
    virtual const std::vector<std::string>& getArgs() const=0;
    
    /* Constructors/initializers for sub-objects */
    virtual IWindow* newWindow(const std::string& title)=0;
    
};

IApplication* IApplicationBootstrap(IApplication::EPlatformType platform,
                                    IApplicationCallback& cb,
                                    const std::string& friendlyName,
                                    const std::string& pname,
                                    const std::vector<std::string>& args);
extern IApplication* APP;
#define IApplicationInstance() APP
    
static inline IApplication* IApplicationBootstrap(IApplication::EPlatformType platform,
                                                  IApplicationCallback& cb,
                                                  const std::string& friendlyName,
                                                  int argc, char** argv)
{
    if (APP)
        return APP;
    std::vector<std::string> args;
    for (int i=1 ; i<argc ; ++i)
        args.push_back(argv[i]);
    return IApplicationBootstrap(platform, cb, friendlyName, argv[0], args);
}
    
}

#endif // IRUNLOOP_HPP
