#ifndef IHIDDEVICE_HPP
#define IHIDDEVICE_HPP

#include "boo/inputdev/DeviceToken.hpp"
#include "boo/inputdev/DeviceBase.hpp"
#include <memory>

namespace boo
{

class IHIDDevice : public std::enable_shared_from_this<IHIDDevice>
{
    friend class DeviceBase;
    friend struct DeviceSignature;
    virtual void _deviceDisconnected()=0;
    virtual bool _sendUSBInterruptTransfer(const uint8_t* data, size_t length)=0;
    virtual size_t _receiveUSBInterruptTransfer(uint8_t* data, size_t length)=0;
    virtual std::vector<uint8_t> _getReportDescriptor()=0;
    virtual bool _sendHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message)=0;
    virtual size_t _receiveHIDReport(uint8_t* data, size_t length, HIDReportType tp, uint32_t message)=0;
    virtual void _startThread()=0;
public:
    virtual ~IHIDDevice() = default;
};

}

#endif // IHIDDEVICE_HPP
