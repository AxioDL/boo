#include "inputdev/SDeviceSignature.hpp"
#include "inputdev/CDeviceToken.hpp"
#include "IHIDDevice.hpp"

namespace boo
{

extern const SDeviceSignature BOO_DEVICE_SIGS[];


bool SDeviceSignature::DeviceMatchToken(const CDeviceToken& token, const TDeviceSignatureSet& sigSet)
{
    for (const SDeviceSignature* sig : sigSet)
    {
        if (sig->m_vid == token.getVendorId() && sig->m_pid == token.getProductId())
            return true;
    }
    return false;
}

IHIDDevice* IHIDDeviceNew(CDeviceToken& token, CDeviceBase& devImp, bool lowLevel);
CDeviceBase* SDeviceSignature::DeviceNew(CDeviceToken& token)
{
    
    CDeviceBase* retval = NULL;
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
    
    IHIDDevice* newDev = IHIDDeviceNew(token, *retval, foundSig->m_lowLevel);
    if (!newDev)
    {
        delete retval;
        return NULL;
    }
    
    return retval;
    
}

}
