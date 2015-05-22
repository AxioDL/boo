#ifndef CDEVICEBASE
#define CDEVICEBASE

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

namespace boo
{

class CDeviceBase
{
    friend class CDeviceToken;
    friend class CHIDDeviceIOKit;
    friend class CHIDDeviceUdev;
    friend class CHIDDeviceWinUSB;

    class CDeviceToken* m_token;
    class IHIDDevice* m_hidDev;
    void _deviceDisconnected();
    
public:
    CDeviceBase(CDeviceToken* token);
    virtual ~CDeviceBase();
    void closeDevice();
    virtual void deviceDisconnected()=0;
    virtual void deviceError(const char* error, ...);
    
    /* Low-Level API */
    bool sendUSBInterruptTransfer(uint8_t pipe, const uint8_t* data, size_t length);
    size_t receiveUSBInterruptTransfer(uint8_t pipe, uint8_t* data, size_t length);
    virtual void initialCycle() {}
    virtual void transferCycle() {}
    virtual void finalCycle() {}

    /* High-Level API */
    bool sendHIDReport(const uint8_t* data, size_t length, uint16_t message=0);
    virtual size_t receiveReport(uint8_t* data, size_t length, uint16_t message=0);
};

}

#endif // CDEVICEBASE
