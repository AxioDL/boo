#include "inputdev/CDeviceBase.hpp"
#include "inputdev/CDeviceToken.hpp"
#include "IHIDDevice.hpp"

CDeviceBase::CDeviceBase(CDeviceToken* token, IHIDDevice* hidDev)
: m_token(token), m_hidDev(hidDev)
{
    hidDev->_setDeviceImp(this);
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


