#include "inputdev/DeviceSignature.hpp"
#include "inputdev/DeviceToken.hpp"
#include "inputdev/GenericPad.hpp"
#include "IHIDDevice.hpp"

namespace boo
{

extern const SDeviceSignature BOO_DEVICE_SIGS[];


bool SDeviceSignature::DeviceMatchToken(const CDeviceToken& token, const TDeviceSignatureSet& sigSet)
{
    if (token.getDeviceType() == CDeviceToken::DEVTYPE_GENERICHID)
        return true;
    for (const SDeviceSignature* sig : sigSet)
    {
        if (sig->m_vid == token.getVendorId() && sig->m_pid == token.getProductId())
            return true;
    }
    return false;
}

IHIDDevice* IHIDDeviceNew(CDeviceToken& token, CDeviceBase& devImp);
CDeviceBase* SDeviceSignature::DeviceNew(CDeviceToken& token)
{
    CDeviceBase* retval = NULL;

    /* Early-return for generic HID devices */
    if (token.getDeviceType() == CDeviceToken::DEVTYPE_GENERICHID)
    {
        retval = new CGenericPad(&token);
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
    const SDeviceSignature* foundSig = NULL;
    const SDeviceSignature* sigIter = BOO_DEVICE_SIGS;
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
