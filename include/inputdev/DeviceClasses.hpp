#ifndef CDEVICECLASSES_HPP
#define CDEVICECLASSES_HPP

#include "CDolphinSmashAdapter.hpp"
#include "CRevolutionPad.hpp"
#include "CCafeProPad.hpp"
#include "CDualshockPad.hpp"
#include "CGenericPad.hpp"

namespace boo
{

#define VID_NINTENDO 0x57e
#define PID_SMASH_ADAPTER 0x337

enum EDeviceMask
{
    DEV_NONE                = 0,
    DEV_DOL_SMASH_ADAPTER   = 1<<0,
    DEV_RVL_PAD             = 1<<1,
    DEV_CAFE_PRO_PAD        = 1<<2,
    DEV_DUALSHOCK_PAD       = 1<<3,
    DEV_GENERIC_PAD         = 1<<4,
    DEV_ALL                 = 0xff
};

bool BooDeviceMatchToken(const CDeviceToken& token, EDeviceMask mask);
CDeviceBase* BooDeviceNew(CDeviceToken& token);

}

#endif // CDEVICECLASSES_HPP
