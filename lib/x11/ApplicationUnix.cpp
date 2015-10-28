/* Meta-implementation for dynamically-constructing user's preferred
 * platform interface
 */

#define APPLICATION_UNIX_CPP
#include "ApplicationXCB.hpp"
#include "ApplicationWayland.hpp"

#include <memory>
#include <dbus/dbus.h>
#include <stdio.h>

DBusConnection* RegisterDBus(const char* appName, bool& isFirst)
{
    isFirst = true;
    DBusError err = {};
    dbus_error_init(&err);

    /* connect to the bus and check for errors */
    DBusConnection* conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err))
    {
       fprintf(stderr, "DBus Connection Error (%s)\n", err.message);
       dbus_error_free(&err);
    }
    if (NULL == conn)
       return NULL;

    /* request our name on the bus and check for errors */
    char busName[256];
    snprintf(busName, 256, "boo.%s.unique", appName);
    int ret = dbus_bus_request_name(conn, busName, DBUS_NAME_FLAG_DO_NOT_QUEUE , &err);
    if (dbus_error_is_set(&err))
    {
       fprintf(stderr, "DBus Name Error (%s)\n", err.message);
       dbus_error_free(&err);
       dbus_connection_close(conn);
       return NULL;
    }
    if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret)
        isFirst = false;

    return conn;

}

namespace boo
{

IApplication* APP = NULL;
std::unique_ptr<IApplication> ApplicationBootstrap(IApplication::EPlatformType platform,
                                                   IApplicationCallback& cb,
                                                   const std::string& uniqueName,
                                                   const std::string& friendlyName,
                                                   const std::string& pname,
                                                   const std::vector<std::string>& args,
                                                   bool singleInstance)
{
    if (APP)
        return std::unique_ptr<IApplication>();
    if (platform == IApplication::PLAT_WAYLAND)
        APP = new ApplicationWayland(cb, uniqueName, friendlyName, pname, args, singleInstance);
    else if (platform == IApplication::PLAT_XCB ||
             platform == IApplication::PLAT_AUTO)
        APP = new ApplicationXCB(cb, uniqueName, friendlyName, pname, args, singleInstance);
    else
        return std::unique_ptr<IApplication>();
    return std::unique_ptr<IApplication>(APP);
}
    
}
