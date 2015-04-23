#ifndef IHIDDEVICE_HPP
#define IHIDDEVICE_HPP

#include "inputdev/CDeviceToken.hpp"
class CDeviceBase;

class IHIDDevice
{
    friend CDeviceBase;
    virtual void _setDeviceImp(CDeviceBase* dev)=0;
    virtual void _deviceDisconnected()=0;
public:
    inline virtual ~IHIDDevice() {};
};

#endif // IHIDDEVICE_HPP
