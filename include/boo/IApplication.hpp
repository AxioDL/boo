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
    virtual int appMain(IApplication*)=0;
    virtual void appQuitting(IApplication*)=0;
    virtual void appFilesOpen(IApplication*, const std::vector<SystemString>&) {}
};

class IApplication
{
    friend class WindowCocoa;
    friend class WindowWayland;
    friend class WindowXlib;
    friend class WindowWin32;
    virtual void _deletedWindow(IWindow* window)=0;
public:
    virtual ~IApplication() {}
    
    enum class EPlatformType
    {
        Auto        = 0,
        Wayland     = 1,
        Xlib        = 2,
        Android     = 3,
        Cocoa       = 4,
        CocoaTouch  = 5,
        Win32       = 6,
        WinRT       = 7,
        Revolution  = 8,
        Cafe        = 9
    };
    virtual EPlatformType getPlatformType() const=0;
    
    virtual int run()=0;
    virtual const SystemString& getUniqueName() const=0;
    virtual const SystemString& getFriendlyName() const=0;
    virtual const SystemString& getProcessName() const=0;
    virtual const std::vector<SystemString>& getArgs() const=0;
    
    /* Constructors/initializers for sub-objects */
    virtual std::shared_ptr<IWindow> newWindow(const SystemString& title, uint32_t drawSamples)=0;
    
};

int
ApplicationRun(IApplication::EPlatformType platform,
               IApplicationCallback& cb,
               const SystemString& uniqueName,
               const SystemString& friendlyName,
               const SystemString& pname,
               const std::vector<SystemString>& args,
               bool singleInstance=true);
extern IApplication* APP;
    
static inline int
ApplicationRun(IApplication::EPlatformType platform,
               IApplicationCallback& cb,
               const SystemString& uniqueName,
               const SystemString& friendlyName,
               int argc, const SystemChar** argv,
               bool singleInstance=true)
{
    if (APP)
        return 1;
    std::vector<SystemString> args;
    for (int i=1 ; i<argc ; ++i)
        args.push_back(argv[i]);
    return ApplicationRun(platform, cb, uniqueName, friendlyName, argv[0], args, singleInstance);
}
    
}

#endif // IAPPLICATION_HPP
