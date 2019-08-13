#pragma once

#include "DeviceBase.hpp"
#include "HIDParser.hpp"
#include <map>
#include <mutex>

namespace boo {

struct IGenericPadCallback {
  virtual void controllerConnected() {}
  virtual void controllerDisconnected() {}
  virtual void valueUpdate(const HIDMainItem& item, int32_t value) {}
};

class GenericPad final : public TDeviceBase<IGenericPadCallback> {
  HIDParser m_parser;

public:
  GenericPad(DeviceToken* token);
  ~GenericPad() override;

  void deviceDisconnected() override;
  void initialCycle() override;
  void receivedHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message) override;

  void enumerateValues(const std::function<bool(const HIDMainItem& item)>& valueCB) const;
};

} // namespace boo
