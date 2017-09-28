#ifndef BOO_AUDIOCOMMON_HPP
#define BOO_AUDIOCOMMON_HPP

#include <soxr.h>

namespace boo
{

/** Pertinent information from audio backend about optimal mixed-audio representation */
struct AudioVoiceEngineMixInfo
{
    double m_sampleRate;
    soxr_datatype_t m_sampleFormat;
    unsigned m_bitsPerSample;
    AudioChannelSet m_channels;
    ChannelMap m_channelMap;
    size_t m_periodFrames;
};

}

#endif // BOO_AUDIOCOMMON_HPP
