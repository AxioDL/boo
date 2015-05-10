#include "inputdev/CDeviceBase.hpp"
#include "inputdev/CDeviceToken.hpp"
#include "IHIDDevice.hpp"

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

bool CDeviceBase::sendUSBInterruptTransfer(const uint8_t* data, size_t length)
{
    if (m_hidDev)
        return m_hidDev->_sendUSBInterruptTransfer(data, length);
    return false;
}

size_t CDeviceBase::receiveUSBInterruptTransfer(uint8_t* data, size_t length)
{
    if (m_hidDev)
        return m_hidDev->_receiveUSBInterruptTransfer(data, length);
    return false;
}

bool CDeviceBase::sendHIDReport(const uint8_t* data, size_t length)
{
    if (m_hidDev)
        return m_hidDev->_sendHIDReport(data, length);
    return false;
}

}
