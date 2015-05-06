/* Meta-implementation for dynamically-constructing user's preferred
 * platform interface
 */

#define CAPPLICATION_UNIX_CPP
#include "CApplicationXCB.hpp"
#include "CApplicationWayland.hpp"

namespace boo
{

IApplication* APP = NULL;
IApplication* IApplicationBootstrap(IApplication::EPlatformType platform,
                                    IApplicationCallback& cb,
                                    const std::string& friendlyName,
                                    const std::string& pname,
                                    const std::vector<std::string>& args)
{
    if (!APP)
    {
        if (platform == IApplication::PLAT_WAYLAND)
            APP = new CApplicationWayland(cb, friendlyName, pname, args);
        else if (platform == IApplication::PLAT_XCB ||
                 platform == IApplication::PLAT_AUTO)
            APP = new CApplicationXCB(cb, friendlyName, pname, args);
        else
            return NULL;
    }
    return APP;
}
    
}
