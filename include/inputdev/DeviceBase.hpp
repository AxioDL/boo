#ifndef CDEVICEBASE
#define CDEVICEBASE

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

namespace boo
{

class DeviceBase
{
    friend class DeviceToken;
    friend class HIDDeviceIOKit;
    friend class HIDDeviceUdev;
    friend class HIDDeviceWinUSB;

    class DeviceToken* m_token;
    class IHIDDevice* m_hidDev;
    void _deviceDisconnected();
    
public:
    DeviceBase(DeviceToken* token);
    virtual ~DeviceBase();
    void closeDevice();
    virtual void deviceDisconnected()=0;
    virtual void deviceError(const char* error) {fprintf(stderr, "%s\n", error);}
    
    /* Low-Level API */
    bool sendUSBInterruptTransfer(const uint8_t* data, size_t length);
    size_t receiveUSBInterruptTransfer(uint8_t* data, size_t length);
    virtual void initialCycle() {}
    virtual void transferCycle() {}
    virtual void finalCycle() {}

    /* High-Level API */
    bool sendHIDReport(const uint8_t* data, size_t length);
    virtual size_t receiveReport(uint8_t* data, size_t length) {(void)data;(void)length;return 0;}
    
};

}

#endif // CDEVICEBASE
