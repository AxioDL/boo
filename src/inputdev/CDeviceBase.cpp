#include "inputdev/CDeviceBase.hpp"
#include "inputdev/CDeviceToken.hpp"
#include "IHIDDevice.hpp"
#include <cstdarg>

namespace boo
{

CDeviceBase::CDeviceBase(CDeviceToken* token)
: m_token(token), m_hidDev(NULL)
{
}

CDeviceBase::~CDeviceBase()
{
    delete m_hidDev;
}

void CDeviceBase::_deviceDisconnected()
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

void CDeviceBase::closeDevice()
{
    if (m_token)
        m_token->_deviceClose();
}

void CDeviceBase::deviceError(const char* error, ...)
{
    va_list vl;
    va_start(vl, error);
    vfprintf(stderr, error, vl);
    va_end(vl);
}

bool CDeviceBase::sendUSBInterruptTransfer(uint8_t pipe, const uint8_t* data, size_t length)
{
    if (m_hidDev)
        return m_hidDev->_sendUSBInterruptTransfer(pipe, data, length);
    return false;
}

size_t CDeviceBase::receiveUSBInterruptTransfer(uint8_t pipe, uint8_t* data, size_t length)
{
    if (m_hidDev)
        return m_hidDev->_receiveUSBInterruptTransfer(pipe, data, length);
    return false;
}

bool CDeviceBase::sendHIDReport(const uint8_t* data, size_t length, uint16_t message)
{
    if (m_hidDev)
        return m_hidDev->_sendHIDReport(data, length, message);
    return false;
}

size_t CDeviceBase::receiveReport(uint8_t* data, size_t length, uint16_t message)
{
    if (m_hidDev)
        return m_hidDev->_recieveReport(data, length, message);
    return false;
}

}
