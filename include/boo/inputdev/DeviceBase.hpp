#ifndef CDEVICEBASE
#define CDEVICEBASE

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <memory>
#include <vector>
#include "boo/System.hpp"

#if _WIN32
#include <hidsdi.h>
#endif

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
    friend class HIDDeviceIOKit;

    class DeviceToken* m_token;
    std::shared_ptr<IHIDDevice> m_hidDev;
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

    /* Low-Level API */
    bool sendUSBInterruptTransfer(const uint8_t* data, size_t length);
    size_t receiveUSBInterruptTransfer(uint8_t* data, size_t length);

    /* High-Level API */
#if _WIN32
    const PHIDP_PREPARSED_DATA getReportDescriptor();
#else
    std::vector<uint8_t> getReportDescriptor();
#endif
    bool sendHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message=0);
    size_t receiveHIDReport(uint8_t* data, size_t length, HIDReportType tp, uint32_t message=0); // Prefer callback version
    virtual void receivedHIDReport(const uint8_t* /*data*/, size_t /*length*/, HIDReportType /*tp*/, uint32_t /*message*/) {}
};

}

#endif // CDEVICEBASE
