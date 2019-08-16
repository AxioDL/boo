/* Meta-implementation for dynamically-constructing user's preferred
 * platform interface
 */

#define APPLICATION_UNIX_CPP

#include <dbus/dbus.h>
#include <cstdint>
#include <unistd.h>
#include "logvisor/logvisor.hpp"
#include "boo/IApplication.hpp"

namespace boo {
static logvisor::Module Log("boo::ApplicationUnix");
IApplication* APP = nullptr;

class ScreenSaverInhibitor {
  DBusConnection* m_dbus;
  uint64_t m_wid;
  DBusPendingCall* m_pending = nullptr;
  uint32_t m_cookie = UINT32_MAX;

  static void Callback(DBusPendingCall* pending, ScreenSaverInhibitor* user_data) {
    user_data->HandleReply();
  }

  void HandleReply() {
    DBusMessage* msg = nullptr;
    DBusError err = DBUS_ERROR_INIT;
    if ((msg = dbus_pending_call_steal_reply(m_pending)) &&
        dbus_message_get_args(msg, &err, DBUS_TYPE_UINT32, &m_cookie, DBUS_TYPE_INVALID)) {
      Log.report(logvisor::Info, fmt("Screen saver inhibited"));
    } else {
      /* Fallback to xdg-screensaver */
      dbus_error_free(&err);
      Log.report(logvisor::Info, fmt("Falling back to xdg-screensaver inhibit"));
      if (!fork()) {
        execlp("xdg-screensaver", "xdg-screensaver", "suspend", fmt::format(fmt("0x{:X}"), m_wid).c_str(), nullptr);
        exit(1);
      }
    }

    if (msg)
      dbus_message_unref(msg);
    dbus_pending_call_unref(m_pending);
    m_pending = nullptr;
  }

public:
  ScreenSaverInhibitor(DBusConnection* dbus, uint64_t wid) : m_dbus(dbus), m_wid(wid) {
#ifndef BOO_MSAN
    DBusMessage* msg =
        dbus_message_new_method_call("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
                                     "org.freedesktop.ScreenSaver", "Inhibit");
    const char* appName = APP->getUniqueName().data();
    const char* reason = "Game Active";
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &appName, DBUS_TYPE_STRING, &reason, DBUS_TYPE_INVALID);
    dbus_connection_send_with_reply(m_dbus, msg, &m_pending, -1);
    dbus_pending_call_set_notify(m_pending, DBusPendingCallNotifyFunction(Callback), this, nullptr);
    dbus_message_unref(msg);
    dbus_connection_flush(m_dbus);
#endif
  }

  ~ScreenSaverInhibitor() {
#ifndef BOO_MSAN
    if (m_cookie != UINT32_MAX) {
      DBusMessage* msg =
          dbus_message_new_method_call("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
                                       "org.freedesktop.ScreenSaver", "UnInhibit");
      dbus_message_append_args(msg, DBUS_TYPE_UINT32, &m_cookie, DBUS_TYPE_INVALID);
      dbus_connection_send(m_dbus, msg, nullptr);
      dbus_message_unref(msg);
      dbus_connection_flush(m_dbus);
    }
#endif
  }
};
}

#include "ApplicationXlib.hpp"
#include "ApplicationWayland.hpp"

#include <memory>
#include <cstdio>

/* No icon by default */
extern "C" const uint8_t MAINICON_NETWM[] __attribute__((weak)) = {};
extern "C" const size_t MAINICON_NETWM_SZ __attribute__((weak)) = 0;

DBusConnection* RegisterDBus(const char* appName, bool& isFirst) {
  isFirst = true;
  DBusError err = {};
  dbus_error_init(&err);

  /* connect to the bus and check for errors */
  DBusConnection* conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
  if (dbus_error_is_set(&err)) {
    fmt::print(stderr, fmt("DBus Connection Error ({})\n"), err.message);
    dbus_error_free(&err);
  }
  if (conn == nullptr)
    return nullptr;

  /* request our name on the bus and check for errors */
  int ret = dbus_bus_request_name(conn, fmt::format(fmt("boo.{}.unique"), appName).c_str(),
                                  DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
  if (dbus_error_is_set(&err)) {
    fmt::print(stderr, fmt("DBus Name Error ({})\n"), err.message);
    dbus_error_free(&err);
    dbus_connection_close(conn);
    return nullptr;
  }
  if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret)
    isFirst = false;

  return conn;
}

namespace boo {

int ApplicationRun(IApplication::EPlatformType platform, IApplicationCallback& cb, std::string_view uniqueName,
                   std::string_view friendlyName, std::string_view pname, const std::vector<std::string>& args,
                   std::string_view gfxApi, uint32_t samples, uint32_t anisotropy, bool deepColor,
                   bool singleInstance) {
  std::string thrName = std::string(friendlyName) + " Main";
  logvisor::RegisterThreadName(thrName.c_str());
  if (APP)
    return 1;
  if (platform == IApplication::EPlatformType::Wayland)
    APP = new ApplicationWayland(cb, uniqueName, friendlyName, pname, args, gfxApi, samples, anisotropy, deepColor,
                                 singleInstance);
  else if (platform == IApplication::EPlatformType::Xlib || platform == IApplication::EPlatformType::Auto)
    APP = new ApplicationXlib(cb, uniqueName, friendlyName, pname, args, gfxApi, samples, anisotropy, deepColor,
                              singleInstance);
  else
    return 1;
  int ret = APP->run();
  delete APP;
  APP = nullptr;
  return ret;
}

} // namespace boo
