#ifndef CAPPLICATION_UNIX_CPP
#error This file may only be included from CApplicationUnix.cpp
#endif

#include "IApplication.hpp"

namespace boo
{
    
IWindow* _CWindowWaylandNew(const std::string& title);
    
class CApplicationWayland final : public IApplication
{
    IApplicationCallback& m_callback;
    const std::string m_friendlyName;
    const std::string m_pname;
    const std::vector<std::string> m_args;
    
    void _deletedWindow(IWindow* window)
    {
        (void)window;
    }
    
public:
    CApplicationWayland(IApplicationCallback& callback,
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
        return PLAT_WAYLAND;
    }
    
    void run()
    {
        
    }

    void quit()
    {

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
        return _CWindowWaylandNew(title);
    }
};
    
}
