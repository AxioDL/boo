#ifndef CDEVICEBASE
#define CDEVICEBASE

class CDeviceToken;
class IHIDDevice;

class CDeviceBase
{
    CDeviceToken* m_token;
    IHIDDevice* m_hidDev;
public:
    inline CDeviceBase(CDeviceToken* token, IHIDDevice* hidDev)
    : m_token(token), m_hidDev(hidDev) {}
    void _deviceDisconnected();
    virtual void deviceDisconnected()=0;
};

#endif // CDEVICEBASE
