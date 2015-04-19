#ifndef CDEVICETOKEN
#define CDEVICETOKEN

#include <string>
#include <IOKit/hid/IOHIDDevice.h>

class CDeviceBase;

class CDeviceToken
{
    std::string m_name;
#if __APPLE__
    
#elif _WIN32
#elif __linux__
#endif
public:
    const std::string& getName() const;
    CDeviceBase* getDevice() const;
};

#endif // CDEVICETOKEN
