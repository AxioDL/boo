#pragma once

#include "boo/audiodev/IMIDIReader.hpp"
#include "boo/audiodev/IMIDIPort.hpp"

namespace boo {

template <class Sender>
class MIDIEncoder : public IMIDIReader {
  Sender& m_sender;
  uint8_t m_status = 0;
  void _sendMessage(const uint8_t* data, size_t len);
  void _sendContinuedValue(uint32_t val);

public:
  MIDIEncoder(Sender& sender) : m_sender(sender) {}

  void noteOff(uint8_t chan, uint8_t key, uint8_t velocity);
  void noteOn(uint8_t chan, uint8_t key, uint8_t velocity);
  void notePressure(uint8_t chan, uint8_t key, uint8_t pressure);
  void controlChange(uint8_t chan, uint8_t control, uint8_t value);
  void programChange(uint8_t chan, uint8_t program);
  void channelPressure(uint8_t chan, uint8_t pressure);
  void pitchBend(uint8_t chan, int16_t pitch);

  void allSoundOff(uint8_t chan);
  void resetAllControllers(uint8_t chan);
  void localControl(uint8_t chan, bool on);
  void allNotesOff(uint8_t chan);
  void omniMode(uint8_t chan, bool on);
  void polyMode(uint8_t chan, bool on);

  void sysex(const void* data, size_t len);
  void timeCodeQuarterFrame(uint8_t message, uint8_t value);
  void songPositionPointer(uint16_t pointer);
  void songSelect(uint8_t song);
  void tuneRequest();

  void startSeq();
  void continueSeq();
  void stopSeq();

  void reset();
};

} // namespace boo
