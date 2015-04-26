#ifndef CDEVICEBASE
#define CDEVICEBASE

#include <stdint.h>
#include <stdlib.h>

class CDeviceBase
{
    friend class CDeviceToken;
    friend class CHIDDeviceIOKit;
    friend class CHIDDeviceUdev;
    friend class CHIDDeviceWin32;

    class CDeviceToken* m_token;
    class IHIDDevice* m_hidDev;
    void _deviceDisconnected();
    
public:
    CDeviceBase(CDeviceToken* token);
    virtual ~CDeviceBase();
    void closeDevice();
    virtual void deviceDisconnected()=0;
    
    /* Low-Level API */
    bool sendInterruptTransfer(uint8_t pipe, const uint8_t* data, size_t length);
    size_t receiveInterruptTransfer(uint8_t pipe, uint8_t* data, size_t length);
    virtual void transferCycle() {};
    virtual void finalCycle() {};

    /* High-Level API */
    bool sendReport(const uint8_t* data, size_t length);
    virtual size_t receiveReport(uint8_t* data, size_t length) {};
    
};

#endif // CDEVICEBASE
