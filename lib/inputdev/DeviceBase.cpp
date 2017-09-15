#include "boo/inputdev/DeviceBase.hpp"
#include "boo/inputdev/DeviceToken.hpp"
#include "IHIDDevice.hpp"
#include <cstdarg>

namespace boo
{

DeviceBase::DeviceBase(DeviceToken* token)
: m_token(token)
{
}

DeviceBase::~DeviceBase()
{
}

void DeviceBase::_deviceDisconnected()
{
    deviceDisconnected();
    m_token = nullptr;
    if (m_hidDev)
    {
        m_hidDev->_deviceDisconnected();
        m_hidDev.reset();
    }
}

void DeviceBase::closeDevice()
{
    if (m_token)
        m_token->_deviceClose();
}

void DeviceBase::deviceError(const char* error, ...)
{
    va_list vl;
    va_start(vl, error);
    vfprintf(stderr, error, vl);
    va_end(vl);
}

bool DeviceBase::sendUSBInterruptTransfer(const uint8_t* data, size_t length)
{
    if (m_hidDev)
        return m_hidDev->_sendUSBInterruptTransfer(data, length);
    return false;
}

size_t DeviceBase::receiveUSBInterruptTransfer(uint8_t* data, size_t length)
{
    if (m_hidDev)
        return m_hidDev->_receiveUSBInterruptTransfer(data, length);
    return false;
}

#if _WIN32
const PHIDP_PREPARSED_DATA DeviceBase::getReportDescriptor()
{
    if (m_hidDev)
        return m_hidDev->_getReportDescriptor();
    return {};
}
#else
std::vector<uint8_t> DeviceBase::getReportDescriptor()
{
    if (m_hidDev)
        return m_hidDev->_getReportDescriptor();
    return {};
}
#endif

bool DeviceBase::sendHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message)
{
    if (m_hidDev)
        return m_hidDev->_sendHIDReport(data, length, tp, message);
    return false;
}

size_t DeviceBase::receiveHIDReport(uint8_t* data, size_t length, HIDReportType tp, uint32_t message)
{
    if (m_hidDev)
        return m_hidDev->_receiveHIDReport(data, length, tp, message);
    return 0;
}

}
