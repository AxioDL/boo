#ifndef CDEVICEBASE
#define CDEVICEBASE

class CDeviceToken;
class IHIDDevice;

class CDeviceBase
{
    CDeviceToken* m_token;
    IHIDDevice* m_hidDev;
    friend CDeviceToken;
    void _deviceDisconnected();
public:
    CDeviceBase(CDeviceToken* token, IHIDDevice* hidDev);
    virtual ~CDeviceBase();
    void closeDevice();
    virtual void deviceDisconnected()=0;
};

#endif // CDEVICEBASE
