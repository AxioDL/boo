#include "inputdev/DeviceClasses.hpp"
#include "inputdev/CDeviceToken.hpp"
#include "IHIDDevice.hpp"

bool BooDeviceMatchToken(const CDeviceToken& token, EDeviceMask mask)
{
    if (mask & DEV_DOL_SMASH_ADAPTER &&
        token.getVendorId() == VID_NINTENDO && token.getProductId() == PID_SMASH_ADAPTER)
        return true;
    return false;
}

IHIDDevice* IHIDDeviceNew(CDeviceToken& token, CDeviceBase& devImp, bool lowLevel);
CDeviceBase* BooDeviceNew(CDeviceToken& token)
{
    
    CDeviceBase* retval = NULL;
    bool lowLevel = false;
    
    if (token.getVendorId() == VID_NINTENDO && token.getProductId() == PID_SMASH_ADAPTER)
    {
        retval = new CDolphinSmashAdapter(&token);
        lowLevel = true;
    }
    
    if (!retval)
        return NULL;
    
    IHIDDevice* newDev = IHIDDeviceNew(token, *retval, lowLevel);
    if (!newDev)
    {
        delete retval;
        return NULL;
    }
    
    return retval;
    
}
