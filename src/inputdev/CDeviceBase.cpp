#include "inputdev/CDeviceBase.hpp"
#include "inputdev/CDeviceToken.hpp"
#include "IHIDDevice.hpp"


void CDeviceBase::_deviceDisconnected()
{
    deviceDisconnected();
    m_token = NULL;
    m_hidDev->_deviceDisconnected();
    m_hidDev = NULL;
}



void CDeviceBase::closeDevice()
{
    if (m_token)
        m_token->_deviceClose();
}


