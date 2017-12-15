#include "boo/inputdev/NintendoPowerA.hpp"
#include <memory.h>
namespace boo
{
NintendoPowerA::NintendoPowerA(DeviceToken* token)
    : TDeviceBase<INintendoPowerACallback>(token)
{

}

NintendoPowerA::~NintendoPowerA()
{
}

void NintendoPowerA::deviceDisconnected()
{
    std::lock_guard<std::mutex> lk(m_callbackLock);
    if (m_callback)
        m_callback->controllerDisconnected();
}

void NintendoPowerA::initialCycle() {}

void NintendoPowerA::transferCycle()
{
    uint8_t payload[8];
    size_t recvSz = receiveUSBInterruptTransfer(payload, sizeof(payload));
    if (recvSz != 8)
        return;

    NintendoPowerAState state = *reinterpret_cast<NintendoPowerAState*>(&payload);

    std::lock_guard<std::mutex> lk(m_callbackLock);
    if (state != m_last && m_callback)
        m_callback->controllerUpdate(state);
    m_last = state;
}

void NintendoPowerA::finalCycle() {}

void NintendoPowerA::receivedHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message)
{
}

bool NintendoPowerAState::operator==(const NintendoPowerAState &other)
{
    return !memcmp(this, &other, sizeof(NintendoPowerAState));
}

bool NintendoPowerAState::operator!=(const NintendoPowerAState &other)
{
    return memcmp(this, &other, sizeof(NintendoPowerAState));
}

}
