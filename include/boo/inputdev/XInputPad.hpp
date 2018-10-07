#pragma once

#include "DeviceBase.hpp"
#include "DeviceSignature.hpp"
#include "boo/System.hpp"

namespace boo
{

struct XInputPadState
{
    uint16_t wButtons;
    uint8_t bLeftTrigger;
    uint8_t bRightTrigger;
    int16_t sThumbLX;
    int16_t sThumbLY;
    int16_t sThumbRX;
    int16_t sThumbRY;
};

enum class EXInputMotor : uint8_t
{
    None  = 0,
    Right = 1<<0,
    Left  = 1<<1,
};
ENABLE_BITWISE_ENUM(EXInputMotor)

class XInputPad;
struct IXInputPadCallback
{
    virtual void controllerDisconnected() {}
    virtual void controllerUpdate(XInputPad& pad, const XInputPadState&) {}
};

class XInputPad final : public TDeviceBase<IXInputPadCallback>
{
    friend class HIDListenerWinUSB;
    uint16_t m_rumbleRequest[2] = {};
    uint16_t m_rumbleState[2] = {};
public:
    XInputPad(DeviceToken* token) : TDeviceBase<IXInputPadCallback>(dev_typeid(XInputPad), token) {}
    void deviceDisconnected()
    {
        std::lock_guard<std::mutex> lk(m_callbackLock);
        if (m_callback)
            m_callback->controllerDisconnected();
    }
    void startRumble(EXInputMotor motors, uint16_t intensity)
    {
        if ((motors & EXInputMotor::Left) != EXInputMotor::None)
            m_rumbleRequest[0] = intensity;
        if ((motors & EXInputMotor::Right) != EXInputMotor::None)
            m_rumbleRequest[1] = intensity;
    }
    void stopRumble(EXInputMotor motors)
    {
        if ((motors & EXInputMotor::Left) != EXInputMotor::None)
            m_rumbleRequest[0] = 0;
        if ((motors & EXInputMotor::Right) != EXInputMotor::None)
            m_rumbleRequest[1] = 0;
    }
};

}

