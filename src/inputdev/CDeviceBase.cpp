#include "inputdev/CDeviceBase.hpp"
#include "inputdev/CDeviceToken.hpp"
#include "IHIDDevice.hpp"


void CDeviceBase::_deviceDisconnected()
{
    m_token->m_connectedDev = NULL;
    m_token = NULL;
    m_hidDev->deviceDisconnected();
    m_hidDev = NULL;
}



