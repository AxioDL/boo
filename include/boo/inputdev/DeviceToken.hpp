#pragma once

#include <string>
#include "DeviceBase.hpp"
#include "DeviceSignature.hpp"

namespace boo {

class DeviceToken {
  friend struct DeviceSignature;
  friend class HIDListenerWinUSB;
  DeviceType m_devType;
  unsigned m_vendorId;
  unsigned m_productId;
  std::string m_vendorName;
  std::string m_productName;
  std::string m_devPath;

  friend class DeviceBase;
  std::shared_ptr<DeviceBase> m_connectedDev;

  friend class DeviceFinder;
  void _deviceClose() {
    if (m_connectedDev)
      m_connectedDev->_deviceDisconnected();
    m_connectedDev = NULL;
  }

public:
  DeviceToken(const DeviceToken&) = delete;
  DeviceToken(DeviceToken&& other) noexcept = default;
  DeviceToken(DeviceType devType, unsigned vid, unsigned pid, const char* vname, const char* pname, const char* path)
  : m_devType(devType), m_vendorId(vid), m_productId(pid), m_devPath(path), m_connectedDev(NULL) {
    if (vname)
      m_vendorName = vname;
    if (pname)
      m_productName = pname;
  }

  DeviceToken& operator=(const DeviceToken&) = delete;
  DeviceToken& operator=(DeviceToken&&) noexcept = default;

  bool operator==(const DeviceToken& rhs) const { return m_devPath == rhs.m_devPath; }
  bool operator<(const DeviceToken& rhs) const { return m_devPath < rhs.m_devPath; }

  DeviceType getDeviceType() const { return m_devType; }
  unsigned getVendorId() const { return m_vendorId; }
  unsigned getProductId() const { return m_productId; }
  std::string_view getVendorName() const { return m_vendorName; }
  std::string_view getProductName() const { return m_productName; }
  std::string_view getDevicePath() const { return m_devPath; }
  bool isDeviceOpen() const { return (m_connectedDev != NULL); }
  std::shared_ptr<DeviceBase> openAndGetDevice() {
    if (!m_connectedDev)
      m_connectedDev = DeviceSignature::DeviceNew(*this);
    return m_connectedDev;
  }
};

} // namespace boo
