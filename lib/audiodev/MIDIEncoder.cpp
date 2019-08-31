#include "boo/audiodev/MIDIEncoder.hpp"

#include <array>

#include "boo/audiodev/IMIDIPort.hpp"
#include "lib/audiodev/MIDICommon.hpp"

namespace boo {
namespace {
template <typename... Args>
constexpr auto MakeCommand(Args&&... args) -> std::array<uint8_t, sizeof...(Args)> {
  return {std::forward<Args>(args)...};
}
} // Anonymous namespace

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
  std::array<uint8_t, 3> send{};
  uint8_t* ptr = nullptr;
  if (val >= 0x4000) {
    ptr = &send[0];
    send[0] = 0x80 | ((val / 0x4000) & 0x7f);
    send[1] = 0x80;
    val &= 0x3fff;
  }

  if (val >= 0x80) {
    if (ptr == nullptr) {
      ptr = &send[1];
    }
    send[1] = 0x80 | ((val / 0x80) & 0x7f);
  }

  if (ptr == nullptr) {
    ptr = &send[2];
  }
  send[2] = val & 0x7f;

  const size_t sendLength = send.size() - (ptr - send.data());
  m_sender.send(ptr, sendLength);
}

template <class Sender>
void MIDIEncoder<Sender>::noteOff(uint8_t chan, uint8_t key, uint8_t velocity) {
  const auto cmd =
      MakeCommand(uint8_t(int(Status::NoteOff) | (chan & 0xf)), uint8_t(key & 0x7f), uint8_t(velocity & 0x7f));
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::noteOn(uint8_t chan, uint8_t key, uint8_t velocity) {
  const auto cmd =
      MakeCommand(uint8_t(int(Status::NoteOn) | (chan & 0xf)), uint8_t(key & 0x7f), uint8_t(velocity & 0x7f));
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::notePressure(uint8_t chan, uint8_t key, uint8_t pressure) {
  const auto cmd =
      MakeCommand(uint8_t(int(Status::NotePressure) | (chan & 0xf)), uint8_t(key & 0x7f), uint8_t(pressure & 0x7f));
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::controlChange(uint8_t chan, uint8_t control, uint8_t value) {
  const auto cmd =
      MakeCommand(uint8_t(int(Status::ControlChange) | (chan & 0xf)), uint8_t(control & 0x7f), uint8_t(value & 0x7f));
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::programChange(uint8_t chan, uint8_t program) {
  const auto cmd = MakeCommand(uint8_t(int(Status::ProgramChange) | (chan & 0xf)), uint8_t(program & 0x7f));
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::channelPressure(uint8_t chan, uint8_t pressure) {
  const auto cmd = MakeCommand(uint8_t(int(Status::ChannelPressure) | (chan & 0xf)), uint8_t(pressure & 0x7f));
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::pitchBend(uint8_t chan, int16_t pitch) {
  const auto cmd = MakeCommand(uint8_t(int(Status::PitchBend) | (chan & 0xf)), uint8_t((pitch % 128) & 0x7f),
                               uint8_t((pitch / 128) & 0x7f));
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::allSoundOff(uint8_t chan) {
  const auto cmd = MakeCommand(uint8_t(int(Status::ControlChange) | (chan & 0xf)), uint8_t{120}, uint8_t{0});
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::resetAllControllers(uint8_t chan) {
  const auto cmd = MakeCommand(uint8_t(int(Status::ControlChange) | (chan & 0xf)), uint8_t{121}, uint8_t{0});
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::localControl(uint8_t chan, bool on) {
  const auto cmd = MakeCommand(uint8_t(int(Status::ControlChange) | (chan & 0xf)), uint8_t{122}, uint8_t(on ? 127 : 0));
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::allNotesOff(uint8_t chan) {
  const auto cmd = MakeCommand(uint8_t(int(Status::ControlChange) | (chan & 0xf)), uint8_t{123}, uint8_t{0});
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::omniMode(uint8_t chan, bool on) {
  const auto cmd = MakeCommand(uint8_t(int(Status::ControlChange) | (chan & 0xf)), uint8_t(on ? 125 : 124), uint8_t{0});
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::polyMode(uint8_t chan, bool on) {
  const auto cmd = MakeCommand(uint8_t(int(Status::ControlChange) | (chan & 0xf)), uint8_t(on ? 127 : 126), uint8_t{0});
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::sysex(const void* data, size_t len) {
  constexpr auto sysexCmd = MakeCommand(uint8_t(Status::SysEx));
  _sendMessage(sysexCmd);

  _sendContinuedValue(len);
  m_sender.send(data, len);

  constexpr auto sysexTermCmd = MakeCommand(uint8_t(Status::SysExTerm));
  _sendMessage(sysexTermCmd);
}

template <class Sender>
void MIDIEncoder<Sender>::timeCodeQuarterFrame(uint8_t message, uint8_t value) {
  const auto cmd =
      MakeCommand(uint8_t(int(Status::TimecodeQuarterFrame)), uint8_t((message & 0x7 << 4) | (value & 0xf)));
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::songPositionPointer(uint16_t pointer) {
  const auto cmd = MakeCommand(uint8_t(int(Status::SongPositionPointer)), uint8_t((pointer % 128) & 0x7f),
                               uint8_t((pointer / 128) & 0x7f));
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::songSelect(uint8_t song) {
  const auto cmd = MakeCommand(uint8_t(int(Status::TimecodeQuarterFrame)), uint8_t(song & 0x7f));
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::tuneRequest() {
  constexpr auto cmd = MakeCommand(uint8_t(Status::TuneRequest));
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::startSeq() {
  constexpr auto cmd = MakeCommand(uint8_t(Status::Start));
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::continueSeq() {
  constexpr auto cmd = MakeCommand(uint8_t(Status::Continue));
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::stopSeq() {
  constexpr auto cmd = MakeCommand(uint8_t(Status::Stop));
  _sendMessage(cmd);
}

template <class Sender>
void MIDIEncoder<Sender>::reset() {
  constexpr auto cmd = MakeCommand(uint8_t(Status::Reset));
  _sendMessage(cmd);
}

template class MIDIEncoder<IMIDIOut>;
template class MIDIEncoder<IMIDIInOut>;

} // namespace boo
