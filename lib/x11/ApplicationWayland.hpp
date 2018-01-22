#ifndef APPLICATION_UNIX_CPP
#error This file may only be included from CApplicationUnix.cpp
#endif

#include "boo/IApplication.hpp"

#include <dbus/dbus.h>
DBusConnection* RegisterDBus(const char* appName, bool& isFirst);

namespace boo
{
    
std::shared_ptr<IWindow> _WindowWaylandNew(std::string_view title);
    
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
                        std::string_view uniqueName,
                        std::string_view friendlyName,
                        std::string_view pname,
                        const std::vector<std::string>& args,
                        std::string_view gfxApi,
                        uint32_t samples,
                        uint32_t anisotropy,
                        bool deepColor,
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
        return EPlatformType::Wayland;
    }
    
    int run()
    {
        return 0;
    }

    std::string_view getUniqueName() const
    {
        return m_uniqueName;
    }

    std::string_view getFriendlyName() const
    {
        return m_friendlyName;
    }
    
    std::string_view getProcessName() const
    {
        return m_pname;
    }
    
    const std::vector<std::string>& getArgs() const
    {
        return m_args;
    }
    
    std::shared_ptr<IWindow> newWindow(std::string_view title)
    {
        return _WindowWaylandNew(title);
    }
};
    
}
