#ifndef IHIDDEVICE_HPP
#define IHIDDEVICE_HPP

#include "inputdev/CDeviceToken.hpp"

namespace boo
{

class IHIDDevice
{
    friend class CDeviceBase;
    virtual void _deviceDisconnected()=0;
    virtual bool _sendUSBInterruptTransfer(uint8_t pipe, const uint8_t* data, size_t length)=0;
    virtual size_t _receiveUSBInterruptTransfer(uint8_t pipe, uint8_t* data, size_t length)=0;
    virtual bool _sendHIDReport(const uint8_t* data, size_t length)=0;
public:
    inline virtual ~IHIDDevice() {};
};

}

#endif // IHIDDEVICE_HPP
