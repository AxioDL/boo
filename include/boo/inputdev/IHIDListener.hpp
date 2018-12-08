#pragma once

#include <unordered_map>
#include <mutex>
#include "DeviceToken.hpp"

namespace boo {

typedef std::unordered_map<std::string, std::unique_ptr<DeviceToken>> TDeviceTokens;
typedef std::pair<TDeviceTokens::iterator, bool> TInsertedDeviceToken;
class DeviceFinder;

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
