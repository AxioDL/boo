#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "boo/System.hpp"
#include "boo/inputdev/DeviceBase.hpp"

namespace boo {

enum class EDolphinControllerType {
  None = 0,
  Normal = 0x10,
  Wavebird = 0x20,
};
ENABLE_BITWISE_ENUM(EDolphinControllerType)

enum class EDolphinControllerButtons {
  Start = 1 << 0,
  Z = 1 << 1,
  R = 1 << 2,
  L = 1 << 3,
  A = 1 << 8,
  B = 1 << 9,
  X = 1 << 10,
  Y = 1 << 11,
  Left = 1 << 12,
  Right = 1 << 13,
  Down = 1 << 14,
  Up = 1 << 15
};
ENABLE_BITWISE_ENUM(EDolphinControllerButtons)

struct DolphinControllerState {
  std::array<int16_t, 2> m_leftStick{};
  std::array<int16_t, 2> m_rightStick{};
  std::array<int16_t, 2> m_analogTriggers{};
  uint16_t m_btns = 0;
  void reset() {
    m_leftStick = {};
    m_rightStick = {};
    m_analogTriggers = {};
    m_btns = 0;
  }
  void clamp();
};

struct IDolphinSmashAdapterCallback {
  virtual void controllerConnected(unsigned idx, EDolphinControllerType type) {
    (void)idx;
    (void)type;
  }
  virtual void controllerDisconnected(unsigned idx) { (void)idx; }
  virtual void controllerUpdate(unsigned idx, EDolphinControllerType type, const DolphinControllerState& state) {
    (void)idx;
    (void)type;
    (void)state;
  }
};

class DolphinSmashAdapter final : public TDeviceBase<IDolphinSmashAdapterCallback> {
  std::array<int16_t, 2> m_leftStickCal{0x7f, 0};
  std::array<int16_t, 2> m_rightStickCal{0x7f, 0};
  std::array<int16_t, 2> m_triggersCal{};
  uint8_t m_knownControllers = 0;
  uint8_t m_rumbleRequest = 0;
  std::array<bool, 4> m_hardStop{};
  uint8_t m_rumbleState = 0xf; /* Force initial send of stop-rumble command */
  void deviceDisconnected() override;
  void initialCycle() override;
  void transferCycle() override;
  void finalCycle() override;

public:
  DolphinSmashAdapter(DeviceToken* token);
  ~DolphinSmashAdapter() override;

  void setCallback(IDolphinSmashAdapterCallback* cb) {
    TDeviceBase<IDolphinSmashAdapterCallback>::setCallback(cb);
    m_knownControllers = 0;
  }

  void startRumble(size_t idx) {
    if (idx >= m_hardStop.size()) {
      return;
    }

    m_rumbleRequest |= 1U << idx;
  }

  void stopRumble(size_t idx, bool hard = false) {
    if (idx >= m_hardStop.size()) {
      return;
    }

    m_rumbleRequest &= ~(1U << idx);
    m_hardStop[idx] = hard;
  }
};

} // namespace boo
