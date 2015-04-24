#ifndef IHIDDEVICE_HPP
#define IHIDDEVICE_HPP

#include "inputdev/CDeviceToken.hpp"
class CDeviceBase;

class IHIDDevice
{
    friend CDeviceBase;
    virtual void _deviceDisconnected()=0;
    virtual bool _sendInterruptTransfer(uint8_t pipe, const uint8_t* data, size_t length)=0;
    virtual size_t _receiveInterruptTransfer(uint8_t pipe, uint8_t* data, size_t length)=0;
    virtual bool _sendReport(const uint8_t* data, size_t length)=0;
public:
    inline virtual ~IHIDDevice() {};
};

#endif // IHIDDEVICE_HPP
