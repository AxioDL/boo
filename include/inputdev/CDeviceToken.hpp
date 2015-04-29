#ifndef CDEVICETOKEN
#define CDEVICETOKEN

#include <string>
#include "CDeviceBase.hpp"
#include "DeviceClasses.hpp"

namespace boo
{

class CDeviceToken
{
    unsigned m_vendorId;
    unsigned m_productId;
    std::string m_vendorName;
    std::string m_productName;
    std::string m_devPath;
    
    friend class CDeviceBase;
    CDeviceBase* m_connectedDev;
    
    friend class CDeviceFinder;
    inline void _deviceClose()
    {
        if (m_connectedDev)
            m_connectedDev->_deviceDisconnected();
        m_connectedDev = NULL;
    }

public:
    CDeviceToken(const CDeviceToken&) = delete;
    CDeviceToken(CDeviceToken&&) = default;
    inline CDeviceToken(unsigned vid, unsigned pid, const char* vname, const char* pname, const char* path)
    : m_vendorId(vid), m_productId(pid), m_devPath(path), m_connectedDev(NULL)
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
    inline const std::string& getDevicePath() const {return m_devPath;}
    inline bool isDeviceOpen() const {return m_connectedDev;}
    inline CDeviceBase* openAndGetDevice()
    {
        if (!m_connectedDev)
            m_connectedDev = BooDeviceNew(*this);
        return m_connectedDev;
    }
    
    inline bool operator ==(const CDeviceToken& rhs) const
    {return m_devPath == rhs.m_devPath;}
    inline bool operator <(const CDeviceToken& rhs) const
    {return m_devPath < rhs.m_devPath;}
};

}

#endif // CDEVICETOKEN
