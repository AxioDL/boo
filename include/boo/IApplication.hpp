#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "boo/IWindow.hpp"
#include "boo/System.hpp"
#include "boo/inputdev/DeviceFinder.hpp"

namespace boo {
class IApplication;

struct IApplicationCallback {
  virtual int appMain(IApplication*) = 0;
  virtual void appQuitting(IApplication*) = 0;
  virtual void appFilesOpen(IApplication*, const std::vector<SystemString>&) {}
};

class IApplication {
  friend class WindowCocoa;
  friend class WindowWayland;
  friend class WindowXlib;
  friend class WindowWin32;
  virtual void _deletedWindow(IWindow* window) = 0;

public:
  virtual ~IApplication() = default;

  enum class EPlatformType {
    Auto = 0,
    Wayland = 1,
    Xlib = 2,
    Android = 3,
    Cocoa = 4,
    CocoaTouch = 5,
    Win32 = 6,
    UWP = 7,
    Revolution = 8,
    Cafe = 9,
    NX = 10,
    Qt = 11
  };
  virtual EPlatformType getPlatformType() const = 0;

  virtual int run() = 0;
  virtual SystemStringView getUniqueName() const = 0;
  virtual SystemStringView getFriendlyName() const = 0;
  virtual SystemStringView getProcessName() const = 0;
  virtual const std::vector<SystemString>& getArgs() const = 0;

  /* Constructors/initializers for sub-objects */
  virtual std::shared_ptr<IWindow> newWindow(SystemStringView title) = 0;
};

int ApplicationRun(IApplication::EPlatformType platform, IApplicationCallback& cb, SystemStringView uniqueName,
                   SystemStringView friendlyName, SystemStringView pname, const std::vector<SystemString>& args,
                   std::string_view gfxApi = {}, uint32_t samples = 1, uint32_t anisotropy = 1, bool deepColor = false,
                   int64_t targetFrameTime = 0, bool singleInstance = true);
extern IApplication* APP;

static inline int ApplicationRun(IApplication::EPlatformType platform, IApplicationCallback& cb,
                                 SystemStringView uniqueName, SystemStringView friendlyName, int argc,
                                 const SystemChar** argv, std::string_view gfxApi = {}, uint32_t samples = 1,
                                 uint32_t anisotropy = 1, bool deepColor = false, int64_t targetFrameTime = 0,
                                 bool singleInstance = true) {
  if (APP)
    return 1;
  std::vector<SystemString> args;
  for (int i = 1; i < argc; ++i)
    args.push_back(argv[i]);
  return ApplicationRun(platform, cb, uniqueName, friendlyName, argv[0], args, gfxApi, samples, anisotropy, deepColor,
                        targetFrameTime, singleInstance);
}

} // namespace boo
