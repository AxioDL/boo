#include "inputdev/SDeviceSignature.hpp"
#include "inputdev/CDolphinSmashAdapter.hpp"

namespace boo
{

const SDeviceSignature BOO_DEVICE_SIGS[] =
{
    DEVICE_SIG(CDolphinSmashAdapter, 0x57e, 0x337),
    DEVICE_SIG_SENTINEL()
};

}
