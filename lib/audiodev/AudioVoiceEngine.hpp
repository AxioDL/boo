#ifndef BOO_AUDIOVOICEENGINE_HPP
#define BOO_AUDIOVOICEENGINE_HPP

#include "boo/audiodev/IAudioVoiceEngine.hpp"
#include "AudioVoice.hpp"
#include "AudioSubmix.hpp"
#include "IAudioMix.hpp"

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
class BaseAudioVoiceEngine : public IAudioVoiceEngine, public IAudioMix
{
protected:
    friend class AudioVoice;
    friend class AudioSubmix;
    friend class AudioVoiceMono;
    friend class AudioVoiceStereo;
    AudioVoiceEngineMixInfo m_mixInfo;
    std::list<AudioVoice*> m_activeVoices;
    std::list<AudioSubmix*> m_activeSubmixes;

    /* Shared scratch buffers for accumulating audio data for resampling */
    std::vector<int16_t> m_scratchIn;
    std::vector<int16_t> m_scratch16;
    std::vector<int32_t> m_scratch32;
    std::vector<float> m_scratchFlt;

    void _pumpAndMixVoices(size_t frames, int16_t* dataOut);
    void _pumpAndMixVoices(size_t frames, int32_t* dataOut);
    void _pumpAndMixVoices(size_t frames, float* dataOut);

    void _unbindFrom(std::list<AudioVoice*>::iterator it);
    void _unbindFrom(std::list<AudioSubmix*>::iterator it);

public:
    std::unique_ptr<IAudioVoice> allocateNewMonoVoice(double sampleRate,
                                                      IAudioVoiceCallback* cb,
                                                      bool dynamicPitch=false);

    std::unique_ptr<IAudioVoice> allocateNewStereoVoice(double sampleRate,
                                                        IAudioVoiceCallback* cb,
                                                        bool dynamicPitch=false);

    std::unique_ptr<IAudioSubmix> allocateNewSubmix(IAudioSubmixCallback* cb);

    const AudioVoiceEngineMixInfo& mixInfo() const;
    AudioChannelSet getAvailableSet() {return m_mixInfo.m_channels;}
    void pumpAndMixVoices() {}
};

}

#endif // BOO_AUDIOVOICEENGINE_HPP
