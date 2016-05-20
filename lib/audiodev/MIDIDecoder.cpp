#include "boo/audiodev/MIDIDecoder.hpp"
#include "MIDICommon.hpp"
#include <memory>

namespace boo
{

static inline uint8_t clamp7(uint8_t val) {return std::max(0, std::min(127, int(val)));}

bool MIDIDecoder::ReadController::readByte(uint8_t& a)
{
    return m_in.receive(&a, 1) != 0;
}

bool MIDIDecoder::ReadController::read2Bytes(uint8_t& a, uint8_t& b)
{
    uint8_t buf[2];
    int len = m_in.receive(buf, 2);
    a = buf[0];
    b = buf[1];
    return len > 1;
}

bool MIDIDecoder::ReadController::readBuffer(void* buf, size_t len)
{
    return m_in.receive(buf, len) == len;
}

uint32_t MIDIDecoder::_readContinuedValue(uint8_t a)
{
    uint32_t ret = a & 0x7f;

    if (a & 0x80)
    {
        ret <<= 7;
        bool good = m_readControl.readByte(a);
        if (!good)
            return ret;
        ret |= a & 0x7f;

        if (a & 0x80)
        {
            ret <<= 7;
            good = m_readControl.readByte(a);
            if (!good)
                return ret;
            ret |= a & 0x7f;
        }
    }

    return ret;
}

bool MIDIDecoder::receiveBytes()
{
    uint8_t a, b;
    bool good = m_readControl.read2Bytes(a, b);
    if (!good)
        return false;

    if (a & 0x80)
        m_status = a;
    else
        b = a;

    uint8_t chan = m_status & 0xf;
    switch (Status(m_status & 0xf0))
    {
    case Status::NoteOff:
    {
        good = m_readControl.read2Bytes(a, b);
        if (!good)
            return false;
        m_out.noteOff(chan, clamp7(a), clamp7(b));
        break;
    }
    case Status::NoteOn:
    {
        good = m_readControl.read2Bytes(a, b);
        if (!good)
            return false;
        m_out.noteOn(chan, clamp7(a), clamp7(b));
        break;
    }
    case Status::NotePressure:
    {
        good = m_readControl.read2Bytes(a, b);
        if (!good)
            return false;
        m_out.notePressure(chan, clamp7(a), clamp7(b));
        break;
    }
    case Status::ControlChange:
    {
        good = m_readControl.read2Bytes(a, b);
        if (!good)
            return false;
        m_out.controlChange(chan, clamp7(a), clamp7(b));
        break;
    }
    case Status::ProgramChange:
    {
        m_out.programChange(chan, clamp7(b));
        break;
    }
    case Status::ChannelPressure:
    {
        m_out.channelPressure(chan, clamp7(b));
        break;
    }
    case Status::PitchBend:
    {
        good = m_readControl.read2Bytes(a, b);
        if (!good)
            return false;
        m_out.pitchBend(chan, clamp7(b) * 128 + clamp7(a));
        break;
    }
    case Status::SysEx:
    {
        switch (Status(m_status & 0xff))
        {
        case Status::SysEx:
        {
            uint32_t len = _readContinuedValue(a);
            std::unique_ptr<uint8_t[]> buf(new uint8_t[len]);
            if (!m_readControl.readBuffer(buf.get(), len))
                return false;
            m_out.sysex(buf.get(), len);
            break;
        }
        case Status::TimecodeQuarterFrame:
        {
            good = m_readControl.read2Bytes(a, b);
            if (!good)
                return false;
            m_out.timeCodeQuarterFrame(a >> 4 & 0x7, a & 0xf);
            break;
        }
        case Status::SongPositionPointer:
        {
            good = m_readControl.read2Bytes(a, b);
            if (!good)
                return false;
            m_out.songPositionPointer(clamp7(b) * 128 + clamp7(a));
            break;
        }
        case Status::SongSelect:
        {
            m_out.songSelect(clamp7(b));
            break;
        }
        case Status::TuneRequest:
            m_out.tuneRequest();
            break;
        case Status::Start:
            m_out.startSeq();
            break;
        case Status::Continue:
            m_out.continueSeq();
            break;
        case Status::Stop:
            m_out.stopSeq();
            break;
        case Status::Reset:
            m_out.reset();
            break;
        case Status::SysExTerm:
        case Status::TimingClock:
        case Status::ActiveSensing:
        default: break;
        }
        break;
    }
    default: break;
    }

    return true;
}

}
