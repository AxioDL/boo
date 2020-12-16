#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "boo/System.hpp"

#if _WIN32
#include <hidsdi.h>
#endif

namespace boo {
class DeviceToken;
class IHIDDevice;

enum class HIDReportType { Input, Output, Feature };

class DeviceBase : public std::enable_shared_from_this<DeviceBase> {
  friend class DeviceToken;
  friend struct DeviceSignature;
  friend class HIDDeviceIOKit;

  uint64_t m_typeHash;
  class DeviceToken* m_token;
  std::shared_ptr<IHIDDevice> m_hidDev;
  void _deviceDisconnected();

public:
  DeviceBase(uint64_t typeHash, DeviceToken* token);
  virtual ~DeviceBase() = default;

  uint64_t getTypeHash() const { return m_typeHash; }

  void closeDevice();

  /* Callbacks */
  virtual void deviceDisconnected() = 0;
  virtual void vdeviceError(fmt::string_view error, fmt::format_args args);
  template <typename S, typename... Args, typename Char = fmt::char_t<S>>
  void deviceError(const S& format, Args&&... args) {
    vdeviceError(fmt::to_string_view<Char>(format),
      fmt::basic_format_args<fmt::buffer_context<Char>>(
        fmt::internal::make_args_checked<Args...>(format, args...)));
  }
  virtual void initialCycle() {}
  virtual void transferCycle() {}
  virtual void finalCycle() {}

  /* Low-Level API */
  bool sendUSBInterruptTransfer(const uint8_t* data, size_t length);
  size_t receiveUSBInterruptTransfer(uint8_t* data, size_t length);

  inline unsigned getVendorId() const;
  inline unsigned getProductId() const;
  inline std::string_view getVendorName() const;
  inline std::string_view getProductName() const;

  /* High-Level API */
#if _WIN32
#if !WINDOWS_STORE
  PHIDP_PREPARSED_DATA getReportDescriptor() const;
#endif
#else
  std::vector<uint8_t> getReportDescriptor() const;
#endif
  bool sendHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message = 0);
  size_t receiveHIDReport(uint8_t* data, size_t length, HIDReportType tp,
                          uint32_t message = 0); // Prefer callback version
  virtual void receivedHIDReport(const uint8_t* /*data*/, size_t /*length*/, HIDReportType /*tp*/,
                                 uint32_t /*message*/) {}
};

template <class CB>
class TDeviceBase : public DeviceBase {
protected:
  std::mutex m_callbackLock;
  CB* m_callback = nullptr;

public:
  TDeviceBase(uint64_t typeHash, DeviceToken* token) : DeviceBase(typeHash, token) {}
  void setCallback(CB* cb) {
    std::lock_guard<std::mutex> lk(m_callbackLock);
    m_callback = cb;
  }
};

} // namespace boo
