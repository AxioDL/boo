#ifndef BOO_MIDIDECODER_HPP
#define BOO_MIDIDECODER_HPP

#include "boo/audiodev/IMIDIReader.hpp"
#include "boo/audiodev/IMIDIPort.hpp"
#include <functional>

namespace boo
{

class MIDIDecoder
{
    IMIDIReader& m_out;
    uint8_t m_status = 0;

    struct ReadController
    {
        IMIDIIn& m_in;
        bool readByte(uint8_t& a);
        bool read2Bytes(uint8_t& a, uint8_t& b);
        bool readBuffer(void* buf, size_t len);
        ReadController(IMIDIIn& in) : m_in(in) {}
    } m_readControl;
    uint32_t _readContinuedValue(uint8_t a);
public:
    MIDIDecoder(IMIDIIn& in, IMIDIReader& out) : m_readControl(in), m_out(out) {}
    bool receiveBytes();
};

}

#endif // BOO_MIDIDECODER_HPP
