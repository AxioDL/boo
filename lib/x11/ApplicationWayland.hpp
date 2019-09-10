#ifndef APPLICATION_UNIX_CPP
#error This file may only be included from CApplicationUnix.cpp
#endif

#include "boo/IApplication.hpp"

#include <dbus/dbus.h>
DBusConnection* RegisterDBus(const char* appName, bool& isFirst);

namespace boo {

std::shared_ptr<IWindow> _WindowWaylandNew(std::string_view title);

class ApplicationWayland final : public IApplication {
  IApplicationCallback& m_callback;
  const std::string m_uniqueName;
  const std::string m_friendlyName;
  const std::string m_pname;
  const std::vector<std::string> m_args;
  bool m_singleInstance;

  void _deletedWindow([[maybe_unused]] IWindow* window) override {}

public:
  ApplicationWayland(IApplicationCallback& callback, std::string_view uniqueName, std::string_view friendlyName,
                     std::string_view pname, const std::vector<std::string>& args, std::string_view gfxApi,
                     uint32_t samples, uint32_t anisotropy, bool deepColor, bool singleInstance)
  : m_callback(callback)
  , m_uniqueName(uniqueName)
  , m_friendlyName(friendlyName)
  , m_pname(pname)
  , m_args(args)
  , m_singleInstance(singleInstance) {
    (void)m_callback;
    (void)m_singleInstance;
  }

  EPlatformType getPlatformType() const override { return EPlatformType::Wayland; }

  int run() override { return 0; }

  std::string_view getUniqueName() const override { return m_uniqueName; }

  std::string_view getFriendlyName() const override { return m_friendlyName; }

  std::string_view getProcessName() const override { return m_pname; }

  const std::vector<std::string>& getArgs() const override { return m_args; }

  std::shared_ptr<IWindow> newWindow(std::string_view title) override { return _WindowWaylandNew(title); }
};

} // namespace boo
