#include "boo/inputdev/DualshockPad.hpp"
#define _USE_MATH_DEFINES
#include <math.h>
#include <iostream>
#include <stdio.h>
#include <memory.h>

#ifdef _WIN32
static inline uint16_t bswap16(uint16_t val) {return _byteswap_ushort(val);}
#else
static inline uint16_t bswap16(uint16_t val) {return __builtin_byteswap(val);}
#endif

#define RAD_TO_DEG (180.0/M_PI)

void hexdump(void *ptr, int buflen) {
    unsigned char *buf = (unsigned char*)ptr;
    int i, j;
    for (i=0; i<buflen; i+=16) {
        printf("%06x: ", i);
        for (j=0; j<16; j++)
            if (i+j < buflen)
                printf("%02x ", buf[i+j]);
            else
                printf("   ");
        printf(" ");
        for (j=0; j<16; j++)
            if (i+j < buflen)
                printf("%c", isprint(buf[i+j]) ? buf[i+j] : '.');
        printf("\n");
    }
}


namespace boo
{
static const uint8_t defaultReport[35] = {
        0x01, 0xff, 0x00, 0xff, 0x00,
        0xff, 0x80, 0x00, 0x00, 0x00,
        0xff, 0x27, 0x10, 0x00, 0x32,
        0xff, 0x27, 0x10, 0x00, 0x32,
        0xff, 0x27, 0x10, 0x00, 0x32,
        0xff, 0x27, 0x10, 0x00, 0x32,
        0x00, 0x00, 0x00, 0x00, 0x00
};

DualshockPad::DualshockPad(DeviceToken* token)
    : DeviceBase(token),
      m_callback(nullptr),
      m_rumbleRequest(0),
      m_rumbleState(0)
{
    memcpy(m_report.buf, defaultReport, 35);
}

DualshockPad::~DualshockPad()
{

}

void DualshockPad::deviceDisconnected()
{
    if (m_callback)
        m_callback->controllerDisconnected();
}

void DualshockPad::initialCycle()
{
    uint8_t setupCommand[4] = {0x42, 0x0c, 0x00, 0x00}; //Tells controller to start sending changes on in pipe
    if (!sendHIDReport(setupCommand, sizeof(setupCommand), 0x03F4))
    {
        deviceError("Unable to send complete packet! Request size %x\n", sizeof(setupCommand));
        return;
    }
    uint8_t btAddr[8];
    receiveReport(btAddr, sizeof(btAddr), 0x03F5);
    for (int i = 0; i < 6; i++)
        m_btAddress[5 - i] = btAddr[i + 2]; // Copy into buffer reversed, so it is LSB first
}

void DualshockPad::transferCycle()
{
    DualshockPadState state;
    size_t recvSz = receiveUSBInterruptTransfer((uint8_t*)&state, 49);
    if (recvSz != 49)
        return;

    for (int i = 0; i < 3; i++)
        state.m_accelerometer[i] = bswap16(state.m_accelerometer[i]);

    state.m_gyrometerZ = bswap16(state.m_gyrometerZ);
    if (m_callback)
        m_callback->controllerUpdate(state);

    if (m_rumbleRequest != m_rumbleState)
    {
        if (m_rumbleRequest & DS3_MOTOR_LEFT)
        {
            m_report.rumble.leftDuration = m_rumbleDuration[0];
            m_report.rumble.leftForce = m_rumbleIntensity[0];
        }
        else
        {
            m_report.rumble.leftDuration = 0;
            m_report.rumble.leftForce = 0;
        }

        if (m_rumbleRequest & DS3_MOTOR_RIGHT)
        {
            m_report.rumble.rightDuration = m_rumbleDuration[0];
            m_report.rumble.rightOn = true;
        }
        else
        {
            m_report.rumble.rightDuration = 0;
            m_report.rumble.rightOn = false;
        }
        sendHIDReport(m_report.buf, sizeof(m_report), 0x0201);
        m_rumbleState = m_rumbleRequest;
    }
    else
    {
        if (state.m_reserved5[8] & 0x80)
            m_rumbleRequest &= ~DS3_MOTOR_RIGHT;
        if (state.m_reserved5[7] & 0x01)
            m_rumbleRequest &= ~DS3_MOTOR_LEFT;
        m_rumbleState = m_rumbleRequest;
        const double zeroG = 511.5; // 1.65/3.3*1023 (1,65V);
        float accXval = -((double)state.m_accelerometer[0] - zeroG);
        float accYval = -((double)state.m_accelerometer[1] - zeroG);
        float accZval = -((double)state.m_accelerometer[2] - zeroG);
        state.accPitch = (atan2(accYval, accZval) + M_PI) * RAD_TO_DEG;
        state.accYaw = (atan2(accXval, accZval) + M_PI) * RAD_TO_DEG;
        state.gyroZ = (state.m_gyrometerZ / 1023.f);
    }

}

void DualshockPad::finalCycle()
{

}

} // boo
