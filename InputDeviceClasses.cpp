#include "boo/inputdev/DeviceSignature.hpp"
#include "boo/inputdev/DolphinSmashAdapter.hpp"
#include "boo/inputdev/DualshockPad.hpp"

namespace boo
{

const DeviceSignature BOO_DEVICE_SIGS[] =
{
    DEVICE_SIG(DolphinSmashAdapter, 0x57e, 0x337),
    DEVICE_SIG(DualshockPad, 0x54c, 0x268),
    DEVICE_SIG_SENTINEL()
};

}
