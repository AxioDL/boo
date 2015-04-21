#include "inputdev/DeviceClasses.hpp"
#include "inputdev/CDeviceToken.hpp"

bool BooDeviceMatchToken(const CDeviceToken& token, EDeviceMask mask)
{
    if (mask & DEV_DOL_SMASH_ADAPTER &&
        token.getVendorId() == 0x57e && token.getProductId() == 0x337)
        return true;
    return false;
}

IHIDDevice* IHIDDeviceNew(CDeviceToken* token);
CDeviceBase* BooDeviceNew(CDeviceToken* token)
{
    IHIDDevice* newDev = IHIDDeviceNew(token);
    
    if (token->getVendorId() == 0x57e && token->getProductId() == 0x337)
        return new CDolphinSmashAdapter(token, newDev);
    
    return NULL;
}
