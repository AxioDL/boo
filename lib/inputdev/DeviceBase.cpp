#include "boo/inputdev/DeviceBase.hpp"
#include "boo/inputdev/DeviceToken.hpp"
#include "IHIDDevice.hpp"
#include <cstdarg>

namespace boo
{

DeviceBase::DeviceBase(DeviceToken* token)
: m_token(token), m_hidDev(NULL)
{
}

DeviceBase::~DeviceBase()
{
    delete m_hidDev;
}

void DeviceBase::_deviceDisconnected()
{
    deviceDisconnected();
    m_token = NULL;
    if (m_hidDev)
    {
        m_hidDev->_deviceDisconnected();
        delete m_hidDev;
        m_hidDev = NULL;
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

bool DeviceBase::sendHIDReport(const uint8_t* data, size_t length, uint16_t message)
{
    if (m_hidDev)
        return m_hidDev->_sendHIDReport(data, length, message);
    return false;
}

size_t DeviceBase::receiveReport(uint8_t* data, size_t length, uint16_t message)
{
    if (m_hidDev)
        return m_hidDev->_recieveReport(data, length, message);
    return false;
}

}
