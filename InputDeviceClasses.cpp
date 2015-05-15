#include "inputdev/SDeviceSignature.hpp"
#include "inputdev/CDolphinSmashAdapter.hpp"
#include "inputdev/CDualshockPad.hpp"

namespace boo
{

const SDeviceSignature BOO_DEVICE_SIGS[] =
{
    DEVICE_SIG(CDolphinSmashAdapter, 0x57e, 0x337),
    DEVICE_SIG(CDualshockController, 0x54c, 0x268),
    DEVICE_SIG_SENTINEL()
};

}
