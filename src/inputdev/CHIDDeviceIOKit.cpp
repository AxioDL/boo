#include "IHIDDevice.hpp"
#include "inputdev/CDeviceToken.hpp"
#include <IOKit/hid/IOHIDLib.h>

class CHIDDeviceIOKit final : public IHIDDevice
{
    IOHIDDeviceRef m_dev;
    
    void _deviceDisconnected()
    {
        IOHIDDeviceClose(m_dev, kIOHIDOptionsTypeNone);
    }
    
public:
    
    CHIDDeviceIOKit(CDeviceToken* token)
    : m_dev(token->getDeviceHandle())
    {
        IOHIDDeviceOpen(m_dev, kIOHIDOptionsTypeNone);
    }
    
    ~CHIDDeviceIOKit()
    {
        IOHIDDeviceClose(m_dev, kIOHIDOptionsTypeNone);
    }
    

};

IHIDDevice* IHIDDeviceNew(CDeviceToken* token)
{
    return new CHIDDeviceIOKit(token);
}
