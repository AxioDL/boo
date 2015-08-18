#ifndef CDUALSHOCKPAD_HPP
#define CDUALSHOCKPAD_HPP
#include <stdint.h>
#include "DeviceBase.hpp"

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
    uint8_t buf[36];
};

enum EDualshockPadButtons
{
    DS3_SELECT   = 1<< 0,
    DS3_L3       = 1<< 1,
    DS3_R3       = 1<< 2,
    DS3_START    = 1<< 3,
    DS3_UP       = 1<< 4,
    DS3_RIGHT    = 1<< 5,
    DS3_DOWN     = 1<< 6,
    DS3_LEFT     = 1<< 7,
    DS3_L2       = 1<< 8,
    DS3_R2       = 1<< 9,
    DS3_L1       = 1<<10,
    DS3_R1       = 1<<11,
    DS3_TRIANGLE = 1<<12,
    DS3_CIRCLE   = 1<<13,
    DS3_CROSS    = 1<<14,
    DS3_SQUARE   = 1<<15
};

enum EDualshockMotor : int
{
    DS3_MOTOR_RIGHT = 1<<0,
    DS3_MOTOR_LEFT  = 1<<1,
};

enum EDualshockLED
{
    DS3_LED_OFF = 0,
    DS3_LED_1   = 1<<1,
    DS3_LED_2   = 1<<2,
    DS3_LED_3   = 1<<3,
    DS3_LED_4   = 1<<4
};

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
    DualshockPad* ctrl = nullptr;
    virtual void controllerDisconnected() {}
    virtual void controllerUpdate(const DualshockPadState&) {}
};

class DualshockPad final : public DeviceBase
{
    IDualshockPadCallback* m_callback;
    uint8_t m_rumbleRequest;
    uint8_t m_rumbleState;
    uint8_t m_rumbleDuration[2];
    uint8_t m_rumbleIntensity[2];
    uint8_t m_led;
    DualshockOutReport m_report;
    uint8_t m_btAddress[6];
    void deviceDisconnected();
    void initialCycle();
    void transferCycle();
    void finalCycle();
public:
    DualshockPad(DeviceToken* token);
    ~DualshockPad();

    inline void setCallback(IDualshockPadCallback* cb)
    { m_callback = cb; if (m_callback) m_callback->ctrl = this; }

    inline void startRumble(int motor, uint8_t duration = 254, uint8_t intensity=255)
    {
        m_rumbleRequest |= motor;
        if (motor & DS3_MOTOR_LEFT)
        {
            m_rumbleDuration[0] = duration;
            m_rumbleIntensity[0] = intensity;
        }
        if (motor & DS3_MOTOR_RIGHT)
        {
            m_rumbleDuration[1] = duration;
            m_rumbleIntensity[1] = intensity;
        }
    }

    inline void stopRumble(int motor)
    {
        m_rumbleRequest &= ~motor;
    }

    inline int getLED()
    {
        return m_led;
    }

    inline void setLED(int led, bool on = true)
    {
        if (on)
            m_led |= led;
        else
            m_led &= ~led;

        setRawLED(led);
    }

    inline void setRawLED(int led)
    {
        m_report.leds = led;
        sendHIDReport(m_report.buf, sizeof(m_report), 0x0201);
    }
};

}

#endif // CDUALSHOCKPAD_HPP
