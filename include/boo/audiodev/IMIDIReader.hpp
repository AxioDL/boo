#pragma once

#include <cstdlib>
#include <cstdint>

namespace boo
{

class IMIDIReader
{
public:
    virtual void noteOff(uint8_t chan, uint8_t key, uint8_t velocity)=0;
    virtual void noteOn(uint8_t chan, uint8_t key, uint8_t velocity)=0;
    virtual void notePressure(uint8_t chan, uint8_t key, uint8_t pressure)=0;
    virtual void controlChange(uint8_t chan, uint8_t control, uint8_t value)=0;
    virtual void programChange(uint8_t chan, uint8_t program)=0;
    virtual void channelPressure(uint8_t chan, uint8_t pressure)=0;
    virtual void pitchBend(uint8_t chan, int16_t pitch)=0;

    virtual void allSoundOff(uint8_t chan)=0;
    virtual void resetAllControllers(uint8_t chan)=0;
    virtual void localControl(uint8_t chan, bool on)=0;
    virtual void allNotesOff(uint8_t chan)=0;
    virtual void omniMode(uint8_t chan, bool on)=0;
    virtual void polyMode(uint8_t chan, bool on)=0;

    virtual void sysex(const void* data, size_t len)=0;
    virtual void timeCodeQuarterFrame(uint8_t message, uint8_t value)=0;
    virtual void songPositionPointer(uint16_t pointer)=0;
    virtual void songSelect(uint8_t song)=0;
    virtual void tuneRequest()=0;

    virtual void startSeq()=0;
    virtual void continueSeq()=0;
    virtual void stopSeq()=0;

    virtual void reset()=0;
};

}

