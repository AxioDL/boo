#ifndef IHIDLISTENER_HPP
#define IHIDLISTENER_HPP

#include <map>
#include <mutex>
#include "CDeviceToken.hpp"
typedef std::map<TDeviceHandle, CDeviceToken> TDeviceTokens;
class CDeviceFinder;

class IHIDListener
{
public:
    virtual ~IHIDListener() {};
    
    /* Automatic device scanning */
    virtual bool startScanning()=0;
    virtual bool stopScanning()=0;
    
    /* Manual device scanning */
    virtual bool scanNow()=0;
    
};

/* Platform-specific constructor */
IHIDListener* IHIDListenerNew(CDeviceFinder& finder);

#endif // IHIDLISTENER_HPP
