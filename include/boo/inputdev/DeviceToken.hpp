#ifndef CDEVICETOKEN
#define CDEVICETOKEN

#include <string>
#include "DeviceBase.hpp"
#include "DeviceSignature.hpp"

namespace boo
{

class DeviceToken
{
public:
    enum class DeviceType
    {
        None       = 0,
        USB        = 1,
        Bluetooth  = 2,
        GenericHID = 3
    };

private:
    DeviceType m_devType;
    unsigned m_vendorId;
    unsigned m_productId;
    std::string m_vendorName;
    std::string m_productName;
    std::string m_devPath;
    
    friend class DeviceBase;
    DeviceBase* m_connectedDev;
    
    friend class DeviceFinder;
    inline void _deviceClose()
    {
        if (m_connectedDev)
            m_connectedDev->_deviceDisconnected();
        m_connectedDev = NULL;
    }

public:

    DeviceToken(const DeviceToken&) = delete;
    DeviceToken(const DeviceToken&& other)
    : m_devType(other.m_devType),
      m_vendorId(other.m_vendorId),
      m_productId(other.m_productId),
      m_vendorName(other.m_vendorName),
      m_productName(other.m_productName),
      m_devPath(other.m_devPath),
      m_connectedDev(other.m_connectedDev)
    {}
    inline DeviceToken(DeviceType devType, unsigned vid, unsigned pid, const char* vname, const char* pname, const char* path)
    : m_devType(devType),
      m_vendorId(vid),
      m_productId(pid),
      m_devPath(path),
      m_connectedDev(NULL)
    {
        if (vname)
            m_vendorName = vname;
        if (pname)
            m_productName = pname;
    }
    
    inline DeviceType getDeviceType() const {return m_devType;}
    inline unsigned getVendorId() const {return m_vendorId;}
    inline unsigned getProductId() const {return m_productId;}
    inline const std::string& getVendorName() const {return m_vendorName;}
    inline const std::string& getProductName() const {return m_productName;}
    inline const std::string& getDevicePath() const {return m_devPath;}
    inline bool isDeviceOpen() const {return (m_connectedDev != NULL);}
    inline DeviceBase* openAndGetDevice()
    {
        if (!m_connectedDev)
            m_connectedDev = DeviceSignature::DeviceNew(*this);
        return m_connectedDev;
    }
    
    inline bool operator ==(const DeviceToken& rhs) const
    {return m_devPath == rhs.m_devPath;}
    inline bool operator <(const DeviceToken& rhs) const
    {return m_devPath < rhs.m_devPath;}
};

}

#endif // CDEVICETOKEN
