#ifndef CDEVICEBASE
#define CDEVICEBASE

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <memory>

namespace boo
{
class DeviceToken;
class IHIDDevice;

enum class HIDReportType
{
    Input,
    Output,
    Feature
};

class DeviceBase : public std::enable_shared_from_this<DeviceBase>
{
    friend class DeviceToken;
    friend struct DeviceSignature;

    class DeviceToken* m_token;
    std::unique_ptr<IHIDDevice> m_hidDev;
    void _deviceDisconnected();
    
public:
    DeviceBase(DeviceToken* token);
    virtual ~DeviceBase();
    void closeDevice();
    
    /* Callbacks */
    virtual void deviceDisconnected()=0;
    virtual void deviceError(const char* error, ...);
    virtual void initialCycle() {}
    virtual void transferCycle() {}
    virtual void finalCycle() {}
    virtual size_t getInputBufferSize() const { return 0; }

    /* Low-Level API */
    bool sendUSBInterruptTransfer(const uint8_t* data, size_t length);
    size_t receiveUSBInterruptTransfer(uint8_t* data, size_t length);

    /* High-Level API */
    bool sendHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message=0);
    size_t receiveHIDReport(uint8_t* data, size_t length, HIDReportType tp, uint32_t message=0); // Prefer callback version
    virtual void receivedHIDReport(const uint8_t* /*data*/, size_t /*length*/, HIDReportType /*tp*/, uint32_t /*message*/) {}
};

}

#endif // CDEVICEBASE
