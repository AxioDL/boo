#ifndef IHIDDEVICE_HPP
#define IHIDDEVICE_HPP

#include "inputdev/CDeviceToken.hpp"
class CDeviceBase;

class IHIDDevice
{
    friend CDeviceBase;
    virtual void _deviceDisconnected()=0;
};

#endif // IHIDDEVICE_HPP
