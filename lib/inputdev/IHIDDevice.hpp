#ifndef IHIDDEVICE_HPP
#define IHIDDEVICE_HPP

#include "boo/inputdev/DeviceToken.hpp"
#include "boo/inputdev/DeviceBase.hpp"

namespace boo
{

class IHIDDevice
{
    friend class DeviceBase;
    virtual void _deviceDisconnected()=0;
    virtual bool _sendUSBInterruptTransfer(const uint8_t* data, size_t length)=0;
    virtual size_t _receiveUSBInterruptTransfer(uint8_t* data, size_t length)=0;
    virtual bool _sendHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message)=0;
    virtual size_t _receiveHIDReport(uint8_t* data, size_t length, HIDReportType tp, uint32_t message)=0;
public:
    inline virtual ~IHIDDevice() {}
};

}

#endif // IHIDDEVICE_HPP
