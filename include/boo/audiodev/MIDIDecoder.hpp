#pragma once

#include "boo/audiodev/IMIDIReader.hpp"
#include "boo/audiodev/IMIDIPort.hpp"
#include <functional>
#include <vector>

namespace boo {

class MIDIDecoder {
  IMIDIReader& m_out;
  uint8_t m_status = 0;
  bool _readContinuedValue(std::vector<uint8_t>::const_iterator& it, std::vector<uint8_t>::const_iterator end,
                           uint32_t& valOut);

public:
  MIDIDecoder(IMIDIReader& out) : m_out(out) {}
  std::vector<uint8_t>::const_iterator receiveBytes(std::vector<uint8_t>::const_iterator begin,
                                                    std::vector<uint8_t>::const_iterator end);
};

} // namespace boo
