#ifndef BOO_AUDIOVOICEENGINE_HPP
#define BOO_AUDIOVOICEENGINE_HPP

#include "boo/audiodev/IAudioVoiceEngine.hpp"
#include "AudioVoice.hpp"

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

/** Base class for managing mixing and sample-rate-conversion amongst active voices */
class BaseAudioVoiceEngine : public IAudioVoiceEngine
{
protected:
    friend class AudioVoice;
    AudioVoiceEngineMixInfo m_mixInfo;
    std::list<AudioVoice*> m_activeVoices;

    void _pumpAndMixVoices(size_t frames, int16_t* dataOut);
    void _pumpAndMixVoices(size_t frames, int32_t* dataOut);
    void _pumpAndMixVoices(size_t frames, float* dataOut);

public:
    std::unique_ptr<IAudioVoice> allocateNewMonoVoice(double sampleRate,
                                                      IAudioVoiceCallback* cb,
                                                      bool dynamicPitch=false);

    std::unique_ptr<IAudioVoice> allocateNewStereoVoice(double sampleRate,
                                                        IAudioVoiceCallback* cb,
                                                        bool dynamicPitch=false);

    const AudioVoiceEngineMixInfo& mixInfo() const {return m_mixInfo;}
    AudioChannelSet getAvailableSet() {return m_mixInfo.m_channels;}
    void pumpAndMixVoices() {}
};

}

#endif // BOO_AUDIOVOICEENGINE_HPP
