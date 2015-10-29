#ifndef APPLICATION_UNIX_CPP
#error This file may only be included from CApplicationUnix.cpp
#endif

#include "boo/IApplication.hpp"

#include <dbus/dbus.h>
DBusConnection* RegisterDBus(const char* appName, bool& isFirst);

namespace boo
{
    
IWindow* _WindowWaylandNew(const std::string& title);
    
class ApplicationWayland final : public IApplication
{
    IApplicationCallback& m_callback;
    const std::string m_uniqueName;
    const std::string m_friendlyName;
    const std::string m_pname;
    const std::vector<std::string> m_args;
    bool m_singleInstance;

    void _deletedWindow(IWindow* window)
    {
        (void)window;
    }
    
public:
    ApplicationWayland(IApplicationCallback& callback,
                        const std::string& uniqueName,
                        const std::string& friendlyName,
                        const std::string& pname,
                        const std::vector<std::string>& args,
                        bool singleInstance)
    : m_callback(callback),
      m_uniqueName(uniqueName),
      m_friendlyName(friendlyName),
      m_pname(pname),
      m_args(args),
      m_singleInstance(singleInstance)
    {}
    
    EPlatformType getPlatformType() const
    {
        return PLAT_WAYLAND;
    }
    
    int run()
    {
        
    }

    const std::string& getUniqueName() const
    {
        return m_uniqueName;
    }

    const std::string& getFriendlyName() const
    {
        return m_friendlyName;
    }
    
    const std::string& getProcessName() const
    {
        return m_pname;
    }
    
    const std::vector<std::string>& getArgs() const
    {
        return m_args;
    }
    
    std::unique_ptr<IWindow> newWindow(const std::string& title)
    {
        return std::unique_ptr<IWindow>(_WindowWaylandNew(title));
    }
};
    
}
