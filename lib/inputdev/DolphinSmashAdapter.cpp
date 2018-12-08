#include "boo/inputdev/DolphinSmashAdapter.hpp"
#include "boo/inputdev/DeviceSignature.hpp"
#include <cstdio>
#include <cstring>

namespace boo {
/*
 * Reference: https://github.com/ToadKing/wii-u-gc-adapter/blob/master/wii-u-gc-adapter.c
 */

DolphinSmashAdapter::DolphinSmashAdapter(DeviceToken* token)
: TDeviceBase<IDolphinSmashAdapterCallback>(dev_typeid(DolphinSmashAdapter), token) {}

DolphinSmashAdapter::~DolphinSmashAdapter() {}

static inline EDolphinControllerType parseType(unsigned char status) {
  EDolphinControllerType type =
      EDolphinControllerType(status) & (EDolphinControllerType::Normal | EDolphinControllerType::Wavebird);
  switch (type) {
  case EDolphinControllerType::Normal:
  case EDolphinControllerType::Wavebird:
    return type;
  default:
    return EDolphinControllerType::None;
  }
}

static inline EDolphinControllerType parseState(DolphinControllerState* stateOut, uint8_t* payload, bool& rumble) {
  memset(stateOut, 0, sizeof(DolphinControllerState));
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

void DolphinSmashAdapter::initialCycle() {
  uint8_t handshakePayload[] = {0x13};
  sendUSBInterruptTransfer(handshakePayload, sizeof(handshakePayload));
}

void DolphinSmashAdapter::transferCycle() {
  uint8_t payload[37];
  size_t recvSz = receiveUSBInterruptTransfer(payload, sizeof(payload));
  if (recvSz != 37 || payload[0] != 0x21)
    return;

  // printf("RECEIVED DATA %zu %02X\n", recvSz, payload[0]);

  std::lock_guard<std::mutex> lk(m_callbackLock);
  if (!m_callback)
    return;

  /* Parse controller states */
  uint8_t* controller = &payload[1];
  uint8_t rumbleMask = 0;
  for (uint32_t i = 0; i < 4; i++, controller += 9) {
    DolphinControllerState state;
    bool rumble = false;
    EDolphinControllerType type = parseState(&state, controller, rumble);
    if (type != EDolphinControllerType::None && !(m_knownControllers & 1 << i)) {
      m_leftStickCal[0] = state.m_leftStick[0];
      m_leftStickCal[1] = state.m_leftStick[1];
      m_rightStickCal[0] = state.m_rightStick[0];
      m_rightStickCal[1] = state.m_rightStick[1];
      m_triggersCal[0] = state.m_analogTriggers[0];
      m_triggersCal[1] = state.m_analogTriggers[1];
      m_knownControllers |= 1 << i;
      m_callback->controllerConnected(i, type);
    } else if (type == EDolphinControllerType::None && (m_knownControllers & 1 << i)) {
      m_knownControllers &= ~(1 << i);
      m_callback->controllerDisconnected(i);
    }
    if (m_knownControllers & 1 << i) {
      state.m_leftStick[0] = state.m_leftStick[0] - m_leftStickCal[0];
      state.m_leftStick[1] = state.m_leftStick[1] - m_leftStickCal[1];
      state.m_rightStick[0] = state.m_rightStick[0] - m_rightStickCal[0];
      state.m_rightStick[1] = state.m_rightStick[1] - m_rightStickCal[1];
      state.m_analogTriggers[0] = state.m_analogTriggers[0] - m_triggersCal[0];
      state.m_analogTriggers[1] = state.m_analogTriggers[1] - m_triggersCal[1];
      m_callback->controllerUpdate(i, type, state);
    }
    rumbleMask |= rumble ? 1 << i : 0;
  }

  /* Send rumble message (if needed) */
  uint8_t rumbleReq = m_rumbleRequest & rumbleMask;
  if (rumbleReq != m_rumbleState) {
    uint8_t rumbleMessage[5] = {0x11};
    for (int i = 0; i < 4; ++i) {
      if (rumbleReq & 1 << i)
        rumbleMessage[i + 1] = 1;
      else if (m_hardStop[i])
        rumbleMessage[i + 1] = 2;
      else
        rumbleMessage[i + 1] = 0;
    }

    sendUSBInterruptTransfer(rumbleMessage, sizeof(rumbleMessage));
    m_rumbleState = rumbleReq;
  }
}

void DolphinSmashAdapter::finalCycle() {
  uint8_t rumbleMessage[5] = {0x11, 0, 0, 0, 0};
  sendUSBInterruptTransfer(rumbleMessage, sizeof(rumbleMessage));
}

void DolphinSmashAdapter::deviceDisconnected() {
  for (uint32_t i = 0; i < 4; i++) {
    if (m_knownControllers & 1 << i) {
      m_knownControllers &= ~(1 << i);
      std::lock_guard<std::mutex> lk(m_callbackLock);
      if (m_callback)
        m_callback->controllerDisconnected(i);
    }
  }
}

/* The following code is derived from pad.c in libogc
 *
 *  Copyright (C) 2004 - 2009
 *  Michael Wiedenbauer (shagkur)
 *  Dave Murphy (WinterMute)
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any
 * damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any
 * purpose, including commercial applications, and to alter it and
 * redistribute it freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you
 *    must not claim that you wrote the original software. If you use
 *    this software in a product, an acknowledgment in the product
 *    documentation would be appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and
 *    must not be misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source
 *    distribution.
 */

static int16_t pad_clampregion[8] = {30, 180, 15, 72, 40, 15, 59, 31};

static void pad_clampstick(int16_t& px, int16_t& py, int16_t max, int16_t xy, int16_t min) {
  int x = px;
  int y = py;
  int signX;
  int signY;
  int d;

  if (x > 0) {
    signX = 1;
  } else {
    signX = -1;
    x = -x;
  }

  if (y > 0) {
    signY = 1;
  } else {
    signY = -1;
    y = -y;
  }

  if (x <= min)
    x = 0;
  else
    x -= min;

  if (y <= min) {
    y = 0;
  } else {
    y -= min;
  }

  if (x == 0 && y == 0) {
    px = py = 0;
    return;
  }

  if (xy * y <= xy * x) {
    d = xy * x + (max - xy) * y;
    if (xy * max < d) {
      x = int16_t(xy * max * x / d);
      y = int16_t(xy * max * y / d);
    }
  } else {
    d = xy * y + (max - xy) * x;
    if (xy * max < d) {
      x = int16_t(xy * max * x / d);
      y = int16_t(xy * max * y / d);
    }
  }

  px = int16_t(signX * x);
  py = int16_t(signY * y);
}

static void pad_clamptrigger(int16_t& trigger) {
  int16_t min, max;

  min = pad_clampregion[0];
  max = pad_clampregion[1];
  if (min > trigger)
    trigger = 0;
  else {
    if (max < trigger)
      trigger = max;
    trigger -= min;
  }
}

void DolphinControllerState::clamp() {
  pad_clampstick(m_leftStick[0], m_leftStick[1], pad_clampregion[3], pad_clampregion[4], pad_clampregion[2]);
  pad_clampstick(m_rightStick[0], m_rightStick[1], pad_clampregion[6], pad_clampregion[7], pad_clampregion[5]);
  pad_clamptrigger(m_analogTriggers[0]);
  pad_clamptrigger(m_analogTriggers[1]);
}
} // namespace boo
