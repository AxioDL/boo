#ifndef CDUALSHOCKPAD_HPP
#define CDUALSHOCKPAD_HPP
#include <stdint.h>
#include <type_traits>
#include "DeviceBase.hpp"
#include "boo/System.hpp"

namespace boo
{

struct DualshockLED
{
    uint8_t timeEnabled;
    uint8_t dutyLength;
    uint8_t enabled;
    uint8_t dutyOff;
    uint8_t dutyOn;
};

struct DualshockRumble
{
    uint8_t padding;
    uint8_t rightDuration;
    bool    rightOn;
    uint8_t leftDuration;
    uint8_t leftForce;
};

union DualshockOutReport
{
    struct
    {
        uint8_t reportId;
        DualshockRumble rumble;
        uint8_t gyro1;
        uint8_t gyro2;
        uint8_t padding[2];
        uint8_t leds;
        DualshockLED led[4];
        DualshockLED reserved;
    };
    uint8_t buf[49];
};

enum class EDualshockPadButtons
{
    Select   = 1<< 0,
    L3       = 1<< 1,
    R3       = 1<< 2,
    Start    = 1<< 3,
    Up       = 1<< 4,
    Right    = 1<< 5,
    Down     = 1<< 6,
    Left     = 1<< 7,
    L2       = 1<< 8,
    R2       = 1<< 9,
    L1       = 1<<10,
    R1       = 1<<11,
    Triangle = 1<<12,
    Circle   = 1<<13,
    Cross    = 1<<14,
    Square   = 1<<15
};

enum class EDualshockMotor : uint8_t
{
    None  = 0,
    Right = 1<<0,
    Left  = 1<<1,
};
ENABLE_BITWISE_ENUM(EDualshockMotor)

enum class EDualshockLED
{
    LED_OFF = 0,
    LED_1   = 1<<1,
    LED_2   = 1<<2,
    LED_3   = 1<<3,
    LED_4   = 1<<4
};
ENABLE_BITWISE_ENUM(EDualshockLED)

struct DualshockPadState
{
    uint8_t  m_reportType;
    uint8_t  m_reserved1;
    uint16_t m_buttonState;
    uint8_t  m_psButtonState;
    uint8_t  m_reserved2;
    uint8_t  m_leftStick[2];
    uint8_t  m_rightStick[2];
    uint8_t  m_reserved3[4];
    uint8_t  m_pressureUp;
    uint8_t  m_pressureRight;
    uint8_t  m_pressureDown;
    uint8_t  m_pressureLeft;
    uint8_t  m_pressureL2;
    uint8_t  m_pressureR2;
    uint8_t  m_pressureL1;
    uint8_t  m_pressureR1;
    uint8_t  m_pressureTriangle;
    uint8_t  m_pressureCircle;
    uint8_t  m_pressureCross;
    uint8_t  m_pressureSquare;
    uint8_t  m_reserved4[3];
    uint8_t  m_charge;
    uint8_t  m_power;
    uint8_t  m_connection;
    uint8_t  m_reserved5[9];
    uint16_t m_accelerometer[3];
    uint16_t m_gyrometerZ;
    // INTERNAL, set by libBoo, do not modify directly!
    float accPitch;
    float accYaw;
    float gyroZ;
};

class DualshockPad;
struct IDualshockPadCallback
{
    virtual void controllerDisconnected() {}
    virtual void controllerUpdate(DualshockPad&, const DualshockPadState&) {}
};

class DualshockPad final : public DeviceBase
{
    IDualshockPadCallback* m_callback;
    EDualshockMotor m_rumbleRequest;
    EDualshockMotor m_rumbleState;
    uint8_t m_rumbleDuration[2];
    uint8_t m_rumbleIntensity[2];
    EDualshockLED m_led;
    DualshockOutReport m_report;
    void deviceDisconnected();
    void initialCycle();
    void transferCycle();
    void finalCycle();
    void receivedHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message);
public:
    DualshockPad(DeviceToken* token);
    ~DualshockPad();

    void setCallback(IDualshockPadCallback* cb) { m_callback = cb; }

    void startRumble(EDualshockMotor motor, uint8_t duration = 254, uint8_t intensity=255)
    {
        m_rumbleRequest |= motor;
        if ((EDualshockMotor(motor) & EDualshockMotor::Left) != EDualshockMotor::None)
        {
            m_rumbleDuration[0] = duration;
            m_rumbleIntensity[0] = intensity;
        }
        if ((EDualshockMotor(motor) & EDualshockMotor::Right) != EDualshockMotor::None)
        {
            m_rumbleDuration[1] = duration;
            m_rumbleIntensity[1] = intensity;
        }
    }

    void stopRumble(int motor)
    {
        m_rumbleRequest &= ~EDualshockMotor(motor);
    }

    EDualshockLED getLED()
    {
        return m_led;
    }

    void setLED(EDualshockLED led, bool on = true)
    {
        if (on)
            m_led |= led;
        else
            m_led &= ~led;

        setRawLED(int(led));
    }

    void setRawLED(int led)
    {
        m_report.leds = led;
        sendHIDReport(m_report.buf, sizeof(m_report), HIDReportType::Output, 0x01);
    }
};

}

#endif // CDUALSHOCKPAD_HPP
