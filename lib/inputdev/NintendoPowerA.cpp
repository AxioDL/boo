#include "boo/inputdev/NintendoPowerA.hpp"

#include <array>
#include <cstring>

#include "boo/inputdev/DeviceSignature.hpp"

namespace boo {
NintendoPowerA::NintendoPowerA(DeviceToken* token)
: TDeviceBase<INintendoPowerACallback>(dev_typeid(NintendoPowerA), token) {}

NintendoPowerA::~NintendoPowerA() = default;

void NintendoPowerA::deviceDisconnected() {
  std::lock_guard lk{m_callbackLock};
  if (m_callback != nullptr) {
    m_callback->controllerDisconnected();
  }
}

void NintendoPowerA::initialCycle() {}

void NintendoPowerA::transferCycle() {
  std::array<uint8_t, 8> payload;
  const size_t recvSz = receiveUSBInterruptTransfer(payload.data(), payload.size());
  if (recvSz != payload.size()) {
    return;
  }

  NintendoPowerAState state;
  std::memcpy(&state, payload.data(), sizeof(state));

  std::lock_guard lk{m_callbackLock};
  if (state != m_last && m_callback != nullptr) {
    m_callback->controllerUpdate(state);
  }
  m_last = state;
}

void NintendoPowerA::finalCycle() {}

void NintendoPowerA::receivedHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message) {}

bool NintendoPowerAState::operator==(const NintendoPowerAState& other) const {
  return std::memcmp(this, &other, sizeof(NintendoPowerAState)) == 0;
}

bool NintendoPowerAState::operator!=(const NintendoPowerAState& other) const { return !operator==(other); }

} // namespace boo
