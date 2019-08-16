#include "boo/inputdev/DeviceBase.hpp"
#include "boo/inputdev/DeviceToken.hpp"
#include "IHIDDevice.hpp"
#include <cstdarg>

namespace boo {

DeviceBase::DeviceBase(uint64_t typeHash, DeviceToken* token) : m_typeHash(typeHash), m_token(token) {}

void DeviceBase::_deviceDisconnected() {
  deviceDisconnected();
  m_token = nullptr;
  if (m_hidDev) {
    m_hidDev->_deviceDisconnected();
    m_hidDev.reset();
  }
}

void DeviceBase::closeDevice() {
  if (m_token)
    m_token->_deviceClose();
}

void DeviceBase::vdeviceError(fmt::string_view error, fmt::format_args args) {
  fmt::vprint(error, args);
}

bool DeviceBase::sendUSBInterruptTransfer(const uint8_t* data, size_t length) {
  if (m_hidDev)
    return m_hidDev->_sendUSBInterruptTransfer(data, length);
  return false;
}

size_t DeviceBase::receiveUSBInterruptTransfer(uint8_t* data, size_t length) {
  if (m_hidDev)
    return m_hidDev->_receiveUSBInterruptTransfer(data, length);
  return false;
}

unsigned DeviceBase::getVendorId() const {
  if (m_token)
    return m_token->getVendorId();
  return -1;
}

unsigned DeviceBase::getProductId() const {
  if (m_token)
    return m_token->getProductId();
  return -1;
}

std::string_view DeviceBase::getVendorName() const {
  if (m_token)
    return m_token->getVendorName();
  return {};
}

std::string_view DeviceBase::getProductName() const {
  if (m_token)
    return m_token->getProductName();
  return {};
}

#if _WIN32
#if !WINDOWS_STORE
PHIDP_PREPARSED_DATA DeviceBase::getReportDescriptor() const {
  if (m_hidDev)
    return m_hidDev->_getReportDescriptor();
  return {};
}
#endif
#else
std::vector<uint8_t> DeviceBase::getReportDescriptor() const {
  if (m_hidDev)
    return m_hidDev->_getReportDescriptor();
  return {};
}
#endif

bool DeviceBase::sendHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message) {
  if (m_hidDev)
    return m_hidDev->_sendHIDReport(data, length, tp, message);
  return false;
}

size_t DeviceBase::receiveHIDReport(uint8_t* data, size_t length, HIDReportType tp, uint32_t message) {
  if (m_hidDev)
    return m_hidDev->_receiveHIDReport(data, length, tp, message);
  return 0;
}

} // namespace boo
