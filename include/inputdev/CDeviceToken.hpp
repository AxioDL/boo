#ifndef CDEVICETOKEN
#define CDEVICETOKEN

#include <string>
#include "CDeviceBase.hpp"
#include "DeviceClasses.hpp"

#if __APPLE__
#include <IOKit/hid/IOHIDLib.h>
typedef IOHIDDeviceRef TDeviceHandle;
#elif _WIN32
#elif __linux__
#endif

class CDeviceToken
{
    unsigned m_vendorId;
    unsigned m_productId;
    std::string m_vendorName;
    std::string m_productName;
    TDeviceHandle m_devHandle;
    
    friend class CDeviceBase;
    CDeviceBase* m_connectedDev;
    
    friend class CDeviceFinder;
    inline void _deviceClose()
    {
        printf("CLOSE %p\n", this);
        if (m_connectedDev)
            m_connectedDev->_deviceDisconnected();
        m_connectedDev = NULL;
    }

public:
    CDeviceToken(const CDeviceToken&) = delete;
    CDeviceToken(CDeviceToken&&) = default;
    inline CDeviceToken(unsigned vid, unsigned pid, const char* vname, const char* pname, TDeviceHandle handle)
    : m_vendorId(vid), m_productId(pid), m_devHandle(handle), m_connectedDev(NULL)
    {
        if (vname)
            m_vendorName = vname;
        if (pname)
            m_productName = pname;
    }
    
    inline unsigned getVendorId() const {return m_vendorId;}
    inline unsigned getProductId() const {return m_productId;}
    inline const std::string& getVendorName() const {return m_vendorName;}
    inline const std::string& getProductName() const {return m_productName;}
    inline TDeviceHandle getDeviceHandle() const {return m_devHandle;}
    inline bool isDeviceOpen() const {return m_connectedDev;}
    inline CDeviceBase* openAndGetDevice()
    {
        printf("OPEN %p\n", this);
        if (!m_connectedDev)
            m_connectedDev = BooDeviceNew(this);
        return m_connectedDev;
    }
    
    inline bool operator ==(const CDeviceToken& rhs) const
    {return m_devHandle == rhs.m_devHandle;}
    inline bool operator <(const CDeviceToken& rhs) const
    {return m_devHandle < rhs.m_devHandle;}
};

#endif // CDEVICETOKEN
