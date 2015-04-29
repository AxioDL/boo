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

bool CDeviceBase::sendInterruptTransfer(uint8_t pipe, const uint8_t* data, size_t length)
{
    if (m_hidDev)
        return m_hidDev->_sendInterruptTransfer(pipe, data, length);
    return false;
}

size_t CDeviceBase::receiveInterruptTransfer(uint8_t pipe, uint8_t* data, size_t length)
{
    if (m_hidDev)
        return m_hidDev->_receiveInterruptTransfer(pipe, data, length);
    return false;
}

bool CDeviceBase::sendReport(const uint8_t* data, size_t length)
{
    if (m_hidDev)
        return m_hidDev->_sendReport(data, length);
    return false;
}

}
