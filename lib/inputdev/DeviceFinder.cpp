#include "boo/inputdev/DeviceFinder.hpp"

#include <cstdio>
#include <cstdlib>

#if _WIN32
#include <Dbt.h>
#include <hidclass.h>
#include <usbiodef.h>
#endif

namespace boo {

DeviceFinder* DeviceFinder::skDevFinder = nullptr;

DeviceFinder::DeviceFinder(std::unordered_set<uint64_t> types) {
  if (skDevFinder) {
    fmt::print(stderr, fmt("only one instance of CDeviceFinder may be constructed"));
    std::abort();
  }
  skDevFinder = this;
  for (const uint64_t& typeHash : types) {
    const DeviceSignature* sigIter = BOO_DEVICE_SIGS;
    while (sigIter->m_name) {
      if (sigIter->m_typeHash == typeHash)
        m_types.push_back(sigIter);
      ++sigIter;
    }
  }
}

DeviceFinder::~DeviceFinder() {
  if (m_listener)
    m_listener->stopScanning();
  skDevFinder = nullptr;
}

bool DeviceFinder::_insertToken(std::unique_ptr<DeviceToken>&& token) {
  if (!DeviceSignature::DeviceMatchToken(*token, m_types)) {
    return false;
  }

  m_tokensLock.lock();
  const TInsertedDeviceToken insertedTok = m_tokens.emplace(token->getDevicePath(), std::move(token));
  m_tokensLock.unlock();
  deviceConnected(*insertedTok.first->second);
  return true;
}

void DeviceFinder::_removeToken(const std::string& path) {
  const auto preCheck = m_tokens.find(path);
  if (preCheck == m_tokens.end()) {
    return;
  }

  DeviceToken& tok = *preCheck->second;
  std::shared_ptr<DeviceBase> dev = tok.m_connectedDev;
  tok._deviceClose();
  deviceDisconnected(tok, dev.get());
  m_tokensLock.lock();
  m_tokens.erase(preCheck);
  m_tokensLock.unlock();
}

bool DeviceFinder::startScanning() {
  if (!m_listener)
    m_listener = IHIDListenerNew(*this);
  if (m_listener)
    return m_listener->startScanning();
  return false;
}

bool DeviceFinder::stopScanning() {
  if (!m_listener)
    m_listener = IHIDListenerNew(*this);
  if (m_listener)
    return m_listener->stopScanning();
  return false;
}

bool DeviceFinder::scanNow() {
  if (!m_listener)
    m_listener = IHIDListenerNew(*this);
  if (m_listener)
    return m_listener->scanNow();
  return false;
}

#if _WIN32 && !WINDOWS_STORE
/* Windows-specific WM_DEVICECHANGED handler */
LRESULT DeviceFinder::winDevChangedHandler(WPARAM wParam, LPARAM lParam) {
  PDEV_BROADCAST_HDR dbh = (PDEV_BROADCAST_HDR)lParam;
  PDEV_BROADCAST_DEVICEINTERFACE dbhi = (PDEV_BROADCAST_DEVICEINTERFACE)lParam;
  DeviceFinder* finder = instance();
  if (!finder)
    return 0;

  if (wParam == DBT_DEVICEARRIVAL) {
    if (dbh->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
      DeviceType type = DeviceType::None;
      if (dbhi->dbcc_classguid == GUID_DEVINTERFACE_USB_DEVICE)
        type = DeviceType::USB;
      else if (dbhi->dbcc_classguid == GUID_DEVINTERFACE_HID)
        type = DeviceType::HID;

      if (type != DeviceType::None) {
#ifdef UNICODE
        char devPath[1024];
        wcstombs(devPath, dbhi->dbcc_name, 1024);
        finder->m_listener->_extDevConnect(devPath);
#else
        finder->m_listener->_extDevConnect(dbhi->dbcc_name);
#endif
      }
    }
  } else if (wParam == DBT_DEVICEREMOVECOMPLETE) {
    if (dbh->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
      DeviceType type = DeviceType::None;
      if (dbhi->dbcc_classguid == GUID_DEVINTERFACE_USB_DEVICE)
        type = DeviceType::USB;
      else if (dbhi->dbcc_classguid == GUID_DEVINTERFACE_HID)
        type = DeviceType::HID;

      if (type != DeviceType::None) {
#ifdef UNICODE
        char devPath[1024];
        wcstombs(devPath, dbhi->dbcc_name, 1024);
        finder->m_listener->_extDevDisconnect(devPath);
#else
        finder->m_listener->_extDevDisconnect(dbhi->dbcc_name);
#endif
      }
    }
  }

  return 0;
}
#endif

} // namespace boo
