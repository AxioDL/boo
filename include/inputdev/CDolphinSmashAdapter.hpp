#ifndef CDOLPHINSMASHADAPTER_HPP
#define CDOLPHINSMASHADAPTER_HPP

#include <stdint.h>
#include "CDeviceBase.hpp"

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

struct SDolphinControllerState
{
    uint8_t m_leftStick[2];
    uint8_t m_rightStick[2];
    uint8_t m_analogTriggers[2];
    uint16_t m_btns;
};

struct IDolphinSmashAdapterCallback
{
    virtual void controllerConnected(unsigned idx, EDolphinControllerType type) {}
    virtual void controllerDisconnected(unsigned idx, EDolphinControllerType type) {}
    virtual void controllerUpdate(unsigned idx, EDolphinControllerType type,
                                  const SDolphinControllerState& state) {}
};

class CDolphinSmashAdapter final : public CDeviceBase
{
    IDolphinSmashAdapterCallback* m_callback;
    uint8_t m_knownControllers;
    uint8_t m_rumbleRequest;
    uint8_t m_rumbleState;
    bool m_didHandshake;
    void deviceDisconnected();
    void initialCycle();
    void transferCycle();
    void finalCycle();
public:
    CDolphinSmashAdapter(CDeviceToken* token);
    ~CDolphinSmashAdapter();
    
    inline void setCallback(IDolphinSmashAdapterCallback* cb)
    {m_callback = cb; m_knownControllers = 0;}
    inline void startRumble(unsigned idx) {if (idx >= 4) return; m_rumbleRequest |= 1<<idx;}
    inline void stopRumble(unsigned idx) {if (idx >= 4) return; m_rumbleRequest &= ~(1<<idx);}
};

}

#endif // CDOLPHINSMASHADAPTER_HPP
