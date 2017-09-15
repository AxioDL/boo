#include "boo/inputdev/DeviceSignature.hpp"
#include "boo/inputdev/DolphinSmashAdapter.hpp"
#include "boo/inputdev/DualshockPad.hpp"
#include "boo/inputdev/GenericPad.hpp"
#include "boo/inputdev/XInputPad.hpp"

namespace boo
{

const DeviceSignature BOO_DEVICE_SIGS[] =
{
    DEVICE_SIG(DolphinSmashAdapter, 0x57e, 0x337, DeviceType::USB),
    DEVICE_SIG(DualshockPad, 0x54c, 0x268, DeviceType::HID),
    DEVICE_SIG(GenericPad, 0, 0, DeviceType::HID),
    DEVICE_SIG(XInputPad, 0, 0, DeviceType::XInput),
    DEVICE_SIG_SENTINEL()
};

}
