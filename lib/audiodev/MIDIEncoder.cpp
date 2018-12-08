#include "boo/audiodev/MIDIEncoder.hpp"
#include "MIDICommon.hpp"

namespace boo {

template <class Sender>
void MIDIEncoder<Sender>::_sendMessage(const uint8_t* data, size_t len) {
  if (data[0] == m_status)
    m_sender.send(data + 1, len - 1);
  else {
    if (data[0] & 0x80)
      m_status = data[0];
    m_sender.send(data, len);
  }
}

template <class Sender>
void MIDIEncoder<Sender>::_sendContinuedValue(uint32_t val) {
  uint8_t send[3] = {};
  uint8_t* ptr = nullptr;
  if (val >= 0x4000) {
    ptr = &send[0];
    send[0] = 0x80 | ((val / 0x4000) & 0x7f);
    send[1] = 0x80;
    val &= 0x3fff;
  }

  if (val >= 0x80) {
    if (!ptr)
      ptr = &send[1];
    send[1] = 0x80 | ((val / 0x80) & 0x7f);
  }

  if (!ptr)
    ptr = &send[2];
  send[2] = val & 0x7f;

  m_sender.send(ptr, 3 - (ptr - send));
}

template <class Sender>
void MIDIEncoder<Sender>::noteOff(uint8_t chan, uint8_t key, uint8_t velocity) {
  uint8_t cmd[3] = {uint8_t(int(Status::NoteOff) | (chan & 0xf)), uint8_t(key & 0x7f), uint8_t(velocity & 0x7f)};
  _sendMessage(cmd, 3);
}

template <class Sender>
void MIDIEncoder<Sender>::noteOn(uint8_t chan, uint8_t key, uint8_t velocity) {
  uint8_t cmd[3] = {uint8_t(int(Status::NoteOn) | (chan & 0xf)), uint8_t(key & 0x7f), uint8_t(velocity & 0x7f)};
  _sendMessage(cmd, 3);
}

template <class Sender>
void MIDIEncoder<Sender>::notePressure(uint8_t chan, uint8_t key, uint8_t pressure) {
  uint8_t cmd[3] = {uint8_t(int(Status::NotePressure) | (chan & 0xf)), uint8_t(key & 0x7f), uint8_t(pressure & 0x7f)};
  _sendMessage(cmd, 3);
}

template <class Sender>
void MIDIEncoder<Sender>::controlChange(uint8_t chan, uint8_t control, uint8_t value) {
  uint8_t cmd[3] = {uint8_t(int(Status::ControlChange) | (chan & 0xf)), uint8_t(control & 0x7f), uint8_t(value & 0x7f)};
  _sendMessage(cmd, 3);
}

template <class Sender>
void MIDIEncoder<Sender>::programChange(uint8_t chan, uint8_t program) {
  uint8_t cmd[2] = {uint8_t(int(Status::ProgramChange) | (chan & 0xf)), uint8_t(program & 0x7f)};
  _sendMessage(cmd, 2);
}

template <class Sender>
void MIDIEncoder<Sender>::channelPressure(uint8_t chan, uint8_t pressure) {
  uint8_t cmd[2] = {uint8_t(int(Status::ChannelPressure) | (chan & 0xf)), uint8_t(pressure & 0x7f)};
  _sendMessage(cmd, 2);
}

template <class Sender>
void MIDIEncoder<Sender>::pitchBend(uint8_t chan, int16_t pitch) {
  uint8_t cmd[3] = {uint8_t(int(Status::PitchBend) | (chan & 0xf)), uint8_t((pitch % 128) & 0x7f),
                    uint8_t((pitch / 128) & 0x7f)};
  _sendMessage(cmd, 3);
}

template <class Sender>
void MIDIEncoder<Sender>::allSoundOff(uint8_t chan) {
  uint8_t cmd[3] = {uint8_t(int(Status::ControlChange) | (chan & 0xf)), 120, 0};
  _sendMessage(cmd, 3);
}

template <class Sender>
void MIDIEncoder<Sender>::resetAllControllers(uint8_t chan) {
  uint8_t cmd[3] = {uint8_t(int(Status::ControlChange) | (chan & 0xf)), 121, 0};
  _sendMessage(cmd, 3);
}

template <class Sender>
void MIDIEncoder<Sender>::localControl(uint8_t chan, bool on) {
  uint8_t cmd[3] = {uint8_t(int(Status::ControlChange) | (chan & 0xf)), 122, uint8_t(on ? 127 : 0)};
  _sendMessage(cmd, 3);
}

template <class Sender>
void MIDIEncoder<Sender>::allNotesOff(uint8_t chan) {
  uint8_t cmd[3] = {uint8_t(int(Status::ControlChange) | (chan & 0xf)), 123, 0};
  _sendMessage(cmd, 3);
}

template <class Sender>
void MIDIEncoder<Sender>::omniMode(uint8_t chan, bool on) {
  uint8_t cmd[3] = {uint8_t(int(Status::ControlChange) | (chan & 0xf)), uint8_t(on ? 125 : 124), 0};
  _sendMessage(cmd, 3);
}

template <class Sender>
void MIDIEncoder<Sender>::polyMode(uint8_t chan, bool on) {
  uint8_t cmd[3] = {uint8_t(int(Status::ControlChange) | (chan & 0xf)), uint8_t(on ? 127 : 126), 0};
  _sendMessage(cmd, 3);
}

template <class Sender>
void MIDIEncoder<Sender>::sysex(const void* data, size_t len) {
  uint8_t cmd = uint8_t(Status::SysEx);
  _sendMessage(&cmd, 1);
  _sendContinuedValue(len);
  m_sender.send(data, len);
  cmd = uint8_t(Status::SysExTerm);
  _sendMessage(&cmd, 1);
}

template <class Sender>
void MIDIEncoder<Sender>::timeCodeQuarterFrame(uint8_t message, uint8_t value) {
  uint8_t cmd[2] = {uint8_t(int(Status::TimecodeQuarterFrame)), uint8_t((message & 0x7 << 4) | (value & 0xf))};
  _sendMessage(cmd, 2);
}

template <class Sender>
void MIDIEncoder<Sender>::songPositionPointer(uint16_t pointer) {
  uint8_t cmd[3] = {uint8_t(int(Status::SongPositionPointer)), uint8_t((pointer % 128) & 0x7f),
                    uint8_t((pointer / 128) & 0x7f)};
  _sendMessage(cmd, 3);
}

template <class Sender>
void MIDIEncoder<Sender>::songSelect(uint8_t song) {
  uint8_t cmd[2] = {uint8_t(int(Status::TimecodeQuarterFrame)), uint8_t(song & 0x7f)};
  _sendMessage(cmd, 2);
}

template <class Sender>
void MIDIEncoder<Sender>::tuneRequest() {
  uint8_t cmd = uint8_t(Status::TuneRequest);
  _sendMessage(&cmd, 1);
}

template <class Sender>
void MIDIEncoder<Sender>::startSeq() {
  uint8_t cmd = uint8_t(Status::Start);
  _sendMessage(&cmd, 1);
}

template <class Sender>
void MIDIEncoder<Sender>::continueSeq() {
  uint8_t cmd = uint8_t(Status::Continue);
  _sendMessage(&cmd, 1);
}

template <class Sender>
void MIDIEncoder<Sender>::stopSeq() {
  uint8_t cmd = uint8_t(Status::Stop);
  _sendMessage(&cmd, 1);
}

template <class Sender>
void MIDIEncoder<Sender>::reset() {
  uint8_t cmd = uint8_t(Status::Reset);
  _sendMessage(&cmd, 1);
}

template class MIDIEncoder<IMIDIOut>;
template class MIDIEncoder<IMIDIInOut>;

} // namespace boo
