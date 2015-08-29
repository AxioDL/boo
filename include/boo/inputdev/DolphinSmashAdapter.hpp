#ifndef CDOLPHINSMASHADAPTER_HPP
#define CDOLPHINSMASHADAPTER_HPP

#include <stdint.h>
#include "DeviceBase.hpp"

namespace boo
{

enum EDolphinControllerType
{
    DOL_TYPE_NONE     = 0,
    DOL_TYPE_NORMAL   = 0x10,
    DOL_TYPE_WAVEBIRD = 0x20,
};

enum EDolphinControllerButtons
{
    DOL_START  = 1<<0,
    DOL_Z      = 1<<1,
    DOL_L      = 1<<2,
    DOL_R      = 1<<3,
    DOL_A      = 1<<8,
    DOL_B      = 1<<9,
    DOL_X      = 1<<10,
    DOL_Y      = 1<<11,
    DOL_LEFT   = 1<<12,
    DOL_RIGHT  = 1<<13,
    DOL_DOWN   = 1<<14,
    DOL_UP     = 1<<15
};

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
