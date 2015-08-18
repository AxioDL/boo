#include "inputdev/DeviceBase.hpp"
#include "inputdev/DeviceToken.hpp"
#include "IHIDDevice.hpp"

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

bool DeviceBase::sendHIDReport(const uint8_t* data, size_t length)
{
    if (m_hidDev)
        return m_hidDev->_sendHIDReport(data, length);
    return false;
}

}
