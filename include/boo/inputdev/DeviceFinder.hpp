#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include "boo/inputdev/DeviceSignature.hpp"
#include "boo/inputdev/DeviceToken.hpp"
#include "boo/inputdev/IHIDListener.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace boo {

class DeviceFinder {
public:
  friend class HIDListenerIOKit;
  friend class HIDListenerUdev;
  friend class HIDListenerWinUSB;
  static inline DeviceFinder* instance() { return skDevFinder; }

private:
  static class DeviceFinder* skDevFinder;

  /* Types this finder is interested in (immutable) */
  DeviceSignature::TDeviceSignatureSet m_types;

  /* Platform-specific USB event registration
   * (for auto-scanning, NULL if not registered) */
  std::unique_ptr<IHIDListener> m_listener;

  /* Set of presently-connected device tokens */
  TDeviceTokens m_tokens;
  std::mutex m_tokensLock;

  /* Friend methods for platform-listener to find/insert/remove
   * tokens with type-filtering */
  bool _hasToken(const std::string& path) const {
    return m_tokens.find(path) != m_tokens.end();
  }
  bool _insertToken(std::unique_ptr<DeviceToken>&& token);
  void _removeToken(const std::string& path);

public:
  class CDeviceTokensHandle {
    DeviceFinder& m_finder;

  public:
    CDeviceTokensHandle(DeviceFinder& finder) : m_finder(finder) { m_finder.m_tokensLock.lock(); }
    ~CDeviceTokensHandle() { m_finder.m_tokensLock.unlock(); }

    TDeviceTokens::iterator begin() noexcept { return m_finder.m_tokens.begin(); }
    TDeviceTokens::iterator end() noexcept { return m_finder.m_tokens.end(); }

    TDeviceTokens::const_iterator begin() const noexcept { return m_finder.m_tokens.begin(); }
    TDeviceTokens::const_iterator end() const noexcept { return m_finder.m_tokens.end(); }
  };

  /* Application must specify its interested device-types */
  DeviceFinder(std::unordered_set<uint64_t> types);
  virtual ~DeviceFinder();

  /* Get interested device-type mask */
  const DeviceSignature::TDeviceSignatureSet& getTypes() const { return m_types; }

  /* Iterable set of tokens */
  CDeviceTokensHandle getTokens() { return CDeviceTokensHandle(*this); }

  /* Automatic device scanning */
  bool startScanning();
  bool stopScanning();

  /* Manual device scanning */
  bool scanNow();

  virtual void deviceConnected(DeviceToken&) {}
  virtual void deviceDisconnected(DeviceToken&, DeviceBase*) {}

#if _WIN32
  /* Windows-specific WM_DEVICECHANGED handler */
  static LRESULT winDevChangedHandler(WPARAM wParam, LPARAM lParam);
#endif
};

} // namespace boo
