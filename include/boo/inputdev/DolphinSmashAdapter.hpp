#ifndef CDOLPHINSMASHADAPTER_HPP
#define CDOLPHINSMASHADAPTER_HPP

#include <stdint.h>
#include "DeviceBase.hpp"

namespace boo
{

enum class EDolphinControllerType
{
    None     = 0,
    Normal   = 0x10,
    Wavebird = 0x20,
};
ENABLE_BITWISE_ENUM(EDolphinControllerType)

enum class EDolphinControllerButtons
{
    Start  = 1<<0,
    Z      = 1<<1,
    L      = 1<<2,
    R      = 1<<3,
    A      = 1<<8,
    B      = 1<<9,
    X      = 1<<10,
    Y      = 1<<11,
    Left   = 1<<12,
    Right  = 1<<13,
    Down   = 1<<14,
    Up     = 1<<15
};
ENABLE_BITWISE_ENUM(EDolphinControllerButtons)

struct DolphinControllerState
{
    int8_t m_leftStick[2] = {0};
    int8_t m_rightStick[2] = {0};
    uint8_t m_analogTriggers[2] = {0};
    uint16_t m_btns = 0;
    void reset()
    {
        m_leftStick[0] = 0;
        m_leftStick[1] = 0;
        m_rightStick[0] = 0;
        m_rightStick[1] = 0;
        m_analogTriggers[0] = 0;
        m_analogTriggers[1] = 0;
        m_btns = 0;
    }
    void clamp();
};

struct IDolphinSmashAdapterCallback
{
    virtual void controllerConnected(unsigned idx, EDolphinControllerType type) {(void)idx;(void)type;}
    virtual void controllerDisconnected(unsigned idx) {(void)idx;}
    virtual void controllerUpdate(unsigned idx, EDolphinControllerType type,
                                  const DolphinControllerState& state) {(void)idx;(void)type;(void)state;}
};

class DolphinSmashAdapter final : public DeviceBase
{
    IDolphinSmashAdapterCallback* m_callback = nullptr;
    uint8_t m_knownControllers = 0;
    uint8_t m_rumbleRequest = 0;
    bool m_hardStop[4] = {false};
    uint8_t m_rumbleState = 0;
    void deviceDisconnected();
    void initialCycle();
    void transferCycle();
    void finalCycle();
public:
    DolphinSmashAdapter(DeviceToken* token);
    ~DolphinSmashAdapter();
    
    void setCallback(IDolphinSmashAdapterCallback* cb)
    {m_callback = cb; m_knownControllers = 0;}
    void startRumble(unsigned idx)
    {if (idx >= 4) return; m_rumbleRequest |= 1<<idx;}
    void stopRumble(unsigned idx, bool hard=false)
    {if (idx >= 4) return; m_rumbleRequest &= ~(1<<idx); m_hardStop[idx] = hard;}
};

}

#endif // CDOLPHINSMASHADAPTER_HPP
