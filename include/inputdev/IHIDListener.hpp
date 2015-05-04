#ifndef IHIDLISTENER_HPP
#define IHIDLISTENER_HPP

#include <unordered_map>
#include <mutex>
#include "CDeviceToken.hpp"

namespace boo
{

typedef std::unordered_map<std::string, CDeviceToken> TDeviceTokens;
typedef std::pair<TDeviceTokens::iterator, bool> TInsertedDeviceToken;
class CDeviceFinder;

class IHIDListener
{
public:
    virtual ~IHIDListener() {}
    
    /* Automatic device scanning */
    virtual bool startScanning()=0;
    virtual bool stopScanning()=0;
    
    /* Manual device scanning */
    virtual bool scanNow()=0;

#if _WIN32
    /* External listener implementation (for Windows) */
    virtual bool _extDevConnect(const char* path)=0;
    virtual bool _extDevDisconnect(const char* path)=0;
#endif
    
};

/* Platform-specific constructor */
IHIDListener* IHIDListenerNew(CDeviceFinder& finder);

}

#endif // IHIDLISTENER_HPP
