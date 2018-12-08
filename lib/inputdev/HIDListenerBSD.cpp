#include "IHIDDevice.hpp"
#include "boo/inputdev/DeviceToken.hpp"
#include "boo/inputdev/DeviceBase.hpp"

namespace boo {

class HIDDeviceBSD final : public IHIDDevice {
  DeviceToken& m_token;
  DeviceBase& m_devImp;

  void _deviceDisconnected() {}
  bool _sendUSBInterruptTransfer(const uint8_t* data, size_t length) { return false; }
  size_t _receiveUSBInterruptTransfer(uint8_t* data, size_t length) { return 0; }
  bool _sendHIDReport(const uint8_t* data, size_t length, uint16_t message) { return false; }
  size_t _recieveReport(const uint8_t* data, size_t length, uint16_t message) { return 0; }

public:
  HIDDeviceBSD(DeviceToken& token, DeviceBase& devImp) : m_token(token), m_devImp(devImp) {}

  ~HIDDeviceBSD() {}
};

std::shared_ptr<IHIDDevice> IHIDDeviceNew(DeviceToken& token, const std::shared_ptr<DeviceBase>& devImp) {
  return std::make_shared<HIDDeviceBSD>(token, devImp);
}
} // namespace boo
