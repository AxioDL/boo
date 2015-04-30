#ifndef CDEVICETOKEN
#define CDEVICETOKEN

#include <string>
#include "CDeviceBase.hpp"
#include "SDeviceSignature.hpp"

namespace boo
{

class CDeviceToken
{
public:
    enum TDeviceType
    {
        DEVTYPE_NONE       = 0,
        DEVTYPE_USB        = 1,
        DEVTYPE_BLUETOOTH  = 2,
        DEVTYPE_GENERICHID = 3
    };

private:
    TDeviceType m_devType;
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
    inline CDeviceToken(enum TDeviceType devType, unsigned vid, unsigned pid, const char* vname, const char* pname, const char* path)
    : m_devType(devType), m_vendorId(vid), m_productId(pid), m_devPath(path), m_connectedDev(NULL)
    {
        if (vname)
            m_vendorName = vname;
        if (pname)
            m_productName = pname;
    }
    
    inline TDeviceType getDeviceType() const {return m_devType;}
    inline unsigned getVendorId() const {return m_vendorId;}
    inline unsigned getProductId() const {return m_productId;}
    inline const std::string& getVendorName() const {return m_vendorName;}
    inline const std::string& getProductName() const {return m_productName;}
    inline const std::string& getDevicePath() const {return m_devPath;}
    inline bool isDeviceOpen() const {return m_connectedDev;}
    inline CDeviceBase* openAndGetDevice()
    {
        if (!m_connectedDev)
            m_connectedDev = SDeviceSignature::DeviceNew(*this);
        return m_connectedDev;
    }
    
    inline bool operator ==(const CDeviceToken& rhs) const
    {return m_devPath == rhs.m_devPath;}
    inline bool operator <(const CDeviceToken& rhs) const
    {return m_devPath < rhs.m_devPath;}
};

}

#endif // CDEVICETOKEN
