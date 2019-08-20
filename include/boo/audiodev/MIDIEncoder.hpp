#pragma once

#include "boo/audiodev/IMIDIReader.hpp"

namespace boo {

template <class Sender>
class MIDIEncoder : public IMIDIReader {
  Sender& m_sender;
  uint8_t m_status = 0;
  void _sendMessage(const uint8_t* data, size_t len);
  void _sendContinuedValue(uint32_t val);

public:
  MIDIEncoder(Sender& sender) : m_sender(sender) {}

  void noteOff(uint8_t chan, uint8_t key, uint8_t velocity) override;
  void noteOn(uint8_t chan, uint8_t key, uint8_t velocity) override;
  void notePressure(uint8_t chan, uint8_t key, uint8_t pressure) override;
  void controlChange(uint8_t chan, uint8_t control, uint8_t value) override;
  void programChange(uint8_t chan, uint8_t program) override;
  void channelPressure(uint8_t chan, uint8_t pressure) override;
  void pitchBend(uint8_t chan, int16_t pitch) override;

  void allSoundOff(uint8_t chan) override;
  void resetAllControllers(uint8_t chan) override;
  void localControl(uint8_t chan, bool on) override;
  void allNotesOff(uint8_t chan) override;
  void omniMode(uint8_t chan, bool on) override;
  void polyMode(uint8_t chan, bool on) override;

  void sysex(const void* data, size_t len) override;
  void timeCodeQuarterFrame(uint8_t message, uint8_t value) override;
  void songPositionPointer(uint16_t pointer) override;
  void songSelect(uint8_t song) override;
  void tuneRequest() override;

  void startSeq() override;
  void continueSeq() override;
  void stopSeq() override;

  void reset() override;
};

} // namespace boo
