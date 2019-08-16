#define _CRT_SECURE_NO_WARNINGS 1 /* STFU MSVC */
#include "IHIDDevice.hpp"
#include "boo/inputdev/DeviceToken.hpp"
#include "boo/inputdev/DeviceBase.hpp"

namespace boo {

class HIDDeviceUWP : public IHIDDevice {
public:
  HIDDeviceUWP(DeviceToken& token, const std::shared_ptr<DeviceBase>& devImp) {}

  void _deviceDisconnected() override {}
  bool _sendUSBInterruptTransfer(const uint8_t* data, size_t length) override { return false; }
  size_t _receiveUSBInterruptTransfer(uint8_t* data, size_t length) override { return 0; }
  bool _sendHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message) override { return false; }
  size_t _receiveHIDReport(uint8_t* data, size_t length, HIDReportType tp, uint32_t message) override { return false; }
  void _startThread() override {}
};

std::shared_ptr<IHIDDevice> IHIDDeviceNew(DeviceToken& token, const std::shared_ptr<DeviceBase>& devImp) {
  return std::make_shared<HIDDeviceUWP>(token, devImp);
}

} // namespace boo
