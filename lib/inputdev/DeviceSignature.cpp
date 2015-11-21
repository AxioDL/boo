#include "boo/inputdev/DeviceSignature.hpp"
#include "boo/inputdev/DeviceToken.hpp"
#include "boo/inputdev/GenericPad.hpp"
#include "IHIDDevice.hpp"

namespace boo
{

extern const DeviceSignature BOO_DEVICE_SIGS[];


bool DeviceSignature::DeviceMatchToken(const DeviceToken& token, const TDeviceSignatureSet& sigSet)
{
    if (token.getDeviceType() == DeviceToken::DeviceType::GenericHID)
        return true;
    for (const DeviceSignature* sig : sigSet)
    {
        if (sig->m_vid == token.getVendorId() && sig->m_pid == token.getProductId())
            return true;
    }
    return false;
}

IHIDDevice* IHIDDeviceNew(DeviceToken& token, DeviceBase& devImp);
DeviceBase* DeviceSignature::DeviceNew(DeviceToken& token)
{
    DeviceBase* retval = NULL;

    /* Early-return for generic HID devices */
    if (token.getDeviceType() == DeviceToken::DeviceType::GenericHID)
    {
        retval = new GenericPad(&token);
        if (!retval)
            return NULL;

        IHIDDevice* newDev = IHIDDeviceNew(token, *retval);
        if (!newDev)
        {
            delete retval;
            return NULL;
        }

        return retval;
    }

    /* Otherwise perform signature-matching to find the appropriate device-factory */
    const DeviceSignature* foundSig = NULL;
    const DeviceSignature* sigIter = BOO_DEVICE_SIGS;
    unsigned targetVid = token.getVendorId();
    unsigned targetPid = token.getProductId();
    while (sigIter->m_name)
    {
        if (sigIter->m_vid == targetVid && sigIter->m_pid == targetPid)
        {
            foundSig = sigIter;
            break;
        }
        ++sigIter;
    }
    if (!foundSig)
        return NULL;

    retval = foundSig->m_factory(&token);
    if (!retval)
        return NULL;
    
    IHIDDevice* newDev = IHIDDeviceNew(token, *retval);
    if (!newDev)
    {
        delete retval;
        return NULL;
    }
    
    return retval;
    
}

}
