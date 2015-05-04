#include "inputdev/CDolphinSmashAdapter.hpp"
#include <stdio.h>
#include <string.h>

namespace boo
{

/*
 * Reference: https://github.com/ToadKing/wii-u-gc-adapter/blob/master/wii-u-gc-adapter.c
 */

CDolphinSmashAdapter::CDolphinSmashAdapter(CDeviceToken* token)
: CDeviceBase(token),
  m_callback(NULL),
  m_knownControllers(0),
  m_rumbleRequest(0),
  m_rumbleState(0)
{
}

CDolphinSmashAdapter::~CDolphinSmashAdapter()
{
}

static inline EDolphinControllerType parseType(unsigned char status)
{
    unsigned char type = status & (DOL_TYPE_NORMAL | DOL_TYPE_WAVEBIRD);
    switch (type)
    {
        case DOL_TYPE_NORMAL:
        case DOL_TYPE_WAVEBIRD:
            return (EDolphinControllerType)type;
        default:
            return DOL_TYPE_NONE;
    }
}

static inline EDolphinControllerType
parseState(SDolphinControllerState* stateOut, uint8_t* payload, bool& rumble)
{
    memset(stateOut, 0, sizeof(SDolphinControllerState));
    unsigned char status = payload[0];
    EDolphinControllerType type = parseType(status);
    
    rumble = ((status & 0x04) != 0) ? true : false;
    
    stateOut->m_btns = (uint16_t)payload[1] << 8 | (uint16_t)payload[2];
    
    stateOut->m_leftStick[0] = payload[3];
    stateOut->m_leftStick[1] = payload[4];
    stateOut->m_rightStick[0] = payload[5];
    stateOut->m_rightStick[1] = payload[6];
    stateOut->m_analogTriggers[0] = payload[7];
    stateOut->m_analogTriggers[1] = payload[8];
    
    return type;
}

void CDolphinSmashAdapter::initialCycle()
{
    uint8_t handshakePayload[] = {0x13};
    sendUSBInterruptTransfer(0, handshakePayload, sizeof(handshakePayload));
}

void CDolphinSmashAdapter::transferCycle()
{
    uint8_t payload[37];
    size_t recvSz = receiveUSBInterruptTransfer(0, payload, sizeof(payload));
    if (recvSz != 37 || payload[0] != 0x21)
        return;
    //printf("RECEIVED DATA %zu %02X\n", recvSz, payload[0]);

    if (!m_callback)
        return;

    /* Parse controller states */
    uint8_t* controller = &payload[1];
    uint8_t rumbleMask = 0;
    for (int i=0 ; i<4 ; i++, controller += 9)
    {
        SDolphinControllerState state;
        bool rumble = false;
        EDolphinControllerType type = parseState(&state, controller, rumble);
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
        if (m_knownControllers & 1<<i)
            m_callback->controllerUpdate(i, type, state);
        rumbleMask |= rumble ? 1<<i : 0;
    }

    /* Send rumble message (if needed) */
    uint8_t rumbleReq = m_rumbleRequest & rumbleMask;
    if (rumbleReq != m_rumbleState)
    {
        uint8_t rumbleMessage[5] = {0x11};
        for (int i=0 ; i<4 ; ++i)
        {
            if (rumbleReq & 1<<i)
                rumbleMessage[i+1] = 1;
            else
                rumbleMessage[i+1] = 0;
        }
        sendUSBInterruptTransfer(0, rumbleMessage, sizeof(rumbleMessage));
        m_rumbleState = rumbleReq;
    }
}

void CDolphinSmashAdapter::finalCycle()
{
    uint8_t rumbleMessage[5] = {0x11, 0, 0, 0, 0};
    sendUSBInterruptTransfer(0, rumbleMessage, sizeof(rumbleMessage));
}

void CDolphinSmashAdapter::deviceDisconnected()
{
    
}

}
