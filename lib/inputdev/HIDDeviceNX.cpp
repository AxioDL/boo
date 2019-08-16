#include "IHIDDevice.hpp"

namespace boo {

class HIDDeviceNX : public IHIDDevice {
  DeviceToken& m_token;
  std::shared_ptr<DeviceBase> m_devImp;
  std::string_view m_devPath;

public:
  HIDDeviceNX(DeviceToken& token, const std::shared_ptr<DeviceBase>& devImp)
  : m_token(token), m_devImp(devImp), m_devPath(token.getDevicePath()) {}

  void _deviceDisconnected() override {}
  bool _sendUSBInterruptTransfer(const uint8_t* data, size_t length) override { return false; }
  size_t _receiveUSBInterruptTransfer(uint8_t* data, size_t length) override { return 0; }
  std::vector<uint8_t> _getReportDescriptor() override { return {}; }
  bool _sendHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message) override { return false; }
  size_t _receiveHIDReport(uint8_t* data, size_t length, HIDReportType tp, uint32_t message) override { return 0; }
  void _startThread() override {}
};

std::shared_ptr<IHIDDevice> IHIDDeviceNew(DeviceToken& token, const std::shared_ptr<DeviceBase>& devImp) {
  return std::make_shared<HIDDeviceNX>(token, devImp);
}

} // namespace boo
