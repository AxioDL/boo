#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "boo/inputdev/DeviceToken.hpp"

namespace boo {
class DeviceFinder;

using TDeviceTokens = std::unordered_map<std::string, std::unique_ptr<DeviceToken>>;
using TInsertedDeviceToken = std::pair<TDeviceTokens::iterator, bool>;

class IHIDListener {
public:
  virtual ~IHIDListener() = default;

  /* Automatic device scanning */
  virtual bool startScanning() = 0;
  virtual bool stopScanning() = 0;

  /* Manual device scanning */
  virtual bool scanNow() = 0;

#if _WIN32 && !WINDOWS_STORE
  /* External listener implementation (for Windows) */
  virtual bool _extDevConnect(const char* path) = 0;
  virtual bool _extDevDisconnect(const char* path) = 0;
#endif
};

/* Platform-specific constructor */
std::unique_ptr<IHIDListener> IHIDListenerNew(DeviceFinder& finder);

} // namespace boo
