#include "inputdev/CDolphinSmashAdapter.hpp"
#include <stdio.h>
#include <string.h>

/* Reference: https://github.com/ToadKing/wii-u-gc-adapter/blob/master/wii-u-gc-adapter.c
 */

static const bool BUTTON_MASK[] =
{
    true,
    true,
    true,
    true,
    false,
    false,
    false,
    false,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true
};

CDolphinSmashAdapter::CDolphinSmashAdapter(CDeviceToken* token)
: CDeviceBase(token),
  m_callback(NULL),
  m_knownControllers(0),
  m_rumbleRequest(0),
  m_rumbleState(0),
  m_didHandshake(false)
{
}

CDolphinSmashAdapter::~CDolphinSmashAdapter()
{
}

static const uint8_t HANDSHAKE_PAYLOAD[] = {0x13};

static inline IDolphinSmashAdapterCallback::EDolphinControllerType
parseType(unsigned char status)
{
    unsigned char type = status & (IDolphinSmashAdapterCallback::DOL_TYPE_NORMAL |
                                   IDolphinSmashAdapterCallback::DOL_TYPE_WAVEBIRD);
    switch (type)
    {
        case IDolphinSmashAdapterCallback::DOL_TYPE_NORMAL:
        case IDolphinSmashAdapterCallback::DOL_TYPE_WAVEBIRD:
            return (IDolphinSmashAdapterCallback::EDolphinControllerType)type;
        default:
            return IDolphinSmashAdapterCallback::DOL_TYPE_NONE;
    }
}

static inline IDolphinSmashAdapterCallback::EDolphinControllerType
parseState(IDolphinSmashAdapterCallback::SDolphinControllerState* stateOut, uint8_t* payload)
{
    memset(stateOut, 0, sizeof(IDolphinSmashAdapterCallback::SDolphinControllerState));
    unsigned char status = payload[0];
    IDolphinSmashAdapterCallback::EDolphinControllerType type = parseType(status);
    
    IDolphinSmashAdapterCallback::EDolphinControllerType extra =
    ((status & 0x04) != 0) ? IDolphinSmashAdapterCallback::DOL_TYPE_RUMBLE :
    IDolphinSmashAdapterCallback::DOL_TYPE_NONE;
    
    stateOut->m_btns = (uint16_t)payload[1] << 8 | (uint16_t)payload[2];
    
    stateOut->m_leftStick[0] = payload[3];
    stateOut->m_leftStick[1] = payload[4] ^ 0xFF;
    stateOut->m_rightStick[0] = payload[5];
    stateOut->m_rightStick[1] = payload[6] ^ 0xFF;
    stateOut->m_analogTriggers[0] = payload[7];
    stateOut->m_analogTriggers[1] = payload[8];
    
    return static_cast<IDolphinSmashAdapterCallback::EDolphinControllerType>(type|extra);
}

void CDolphinSmashAdapter::transferCycle()
{
    if (!m_didHandshake)
    {
        if (!sendInterruptTransfer(0, HANDSHAKE_PAYLOAD, sizeof(HANDSHAKE_PAYLOAD)))
            return;
        //printf("SENT HANDSHAKE %d\n", res);
        m_didHandshake = true;
    }
    else
    {
        uint8_t payload[37];
        size_t recvSz = receiveInterruptTransfer(0, payload, sizeof(payload));
        if (recvSz != 37 || payload[0] != 0x21)
            return;
        //printf("RECEIVED DATA %zu %02X\n", recvSz, payload[0]);
        
        if (!m_callback)
            return;
        
        /* Parse controller states */
        uint8_t* controller = &payload[1];
        bool rumbleMask[4] = {false};
        for (int i=0 ; i<4 ; i++, controller += 9)
        {
            IDolphinSmashAdapterCallback::SDolphinControllerState state;
            IDolphinSmashAdapterCallback::EDolphinControllerType type = parseState(&state, controller);
            if (type && !(m_knownControllers & 1<<i))
            {
                m_knownControllers |= 1<<i;
                m_callback->controllerConnected(i, type);
            }
            else if (!type && (m_knownControllers & 1<<i))
            {
                m_knownControllers &= ~(1<<i);
                m_callback->controllerDisconnected(i, type);
            }
            m_callback->controllerUpdate(i, type, state);
            rumbleMask[i] = type & IDolphinSmashAdapterCallback::DOL_TYPE_RUMBLE;
        }
        
        /* Send rumble message (if needed) */
        uint8_t rumbleReq = m_rumbleRequest;
        if (rumbleReq != m_rumbleState)
        {
            uint8_t rumbleMessage[5] = {0x11};
            for (int i=0 ; i<4 ; ++i)
            {
                if (rumbleReq & 1<<i && rumbleMask[i])
                    rumbleMessage[i+1] = 1;
                else
                    rumbleMessage[i+1] = 0;
            }
            sendInterruptTransfer(0, rumbleMessage, sizeof(rumbleMessage));
            m_rumbleState = rumbleReq;
        }
    }
};

void CDolphinSmashAdapter::deviceDisconnected()
{
    
}
