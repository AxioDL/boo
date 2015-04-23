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

IHIDDevice* IHIDDeviceNew(CDeviceToken* token);
CDeviceBase* BooDeviceNew(CDeviceToken* token)
{
    IHIDDevice* newDev = IHIDDeviceNew(token);
    if (!newDev)
        return NULL;
    
    if (token->getVendorId() == VID_NINTENDO && token->getProductId() == PID_SMASH_ADAPTER)
        return new CDolphinSmashAdapter(token, newDev);
    else
        delete newDev;
    
    return NULL;
}
