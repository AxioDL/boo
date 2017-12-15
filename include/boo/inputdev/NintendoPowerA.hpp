#ifndef NINTENDOPOWERA_HPP
#define NINTENDOPOWERA_HPP
#include "DeviceBase.hpp"
#include "boo/System.hpp"

namespace boo
{
struct NintendoPowerAState
{
    uint8_t y : 1;
    uint8_t b : 1;
    uint8_t a : 1;
    uint8_t x : 1;
    uint8_t l : 1;
    uint8_t r : 1;
    uint8_t zl : 1;
    uint8_t zr : 1;
    uint8_t minus : 1;
    uint8_t plus : 1;
    uint8_t stickL : 1;
    uint8_t stickR : 1;
    uint8_t home : 1;
    uint8_t capture : 1;
    uint8_t dPad;
    uint8_t leftX;
    uint8_t leftY;
    uint8_t rightX;
    uint8_t rightY;
    bool operator==(const NintendoPowerAState& other);
    bool operator!=(const NintendoPowerAState& other);
};

class NintendoPowerA;
struct INintendoPowerACallback
{
    virtual void controllerDisconnected() {}
    virtual void controllerUpdate(const NintendoPowerAState& state) {}
};

class NintendoPowerA final : public TDeviceBase<INintendoPowerACallback>
{
    NintendoPowerAState m_last;
    void deviceDisconnected();
    void initialCycle();
    void transferCycle();
    void finalCycle();
    void receivedHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message);
public:
    NintendoPowerA(DeviceToken*);
    ~NintendoPowerA();
};
}

#endif // NINTENDOPWERA_HPP
