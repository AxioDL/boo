#ifndef BOO_AUDIOVOICEENGINE_HPP
#define BOO_AUDIOVOICEENGINE_HPP

#include "boo/audiodev/IAudioVoiceEngine.hpp"
#include "AudioVoice.hpp"
#include "AudioSubmix.hpp"
#include <functional>

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
    friend class AudioSubmix;
    friend class AudioVoiceMono;
    friend class AudioVoiceStereo;
    float m_totalVol = 1.f;
    AudioVoiceEngineMixInfo m_mixInfo;
    std::list<AudioVoice*> m_activeVoices;
    std::list<AudioSubmix*> m_activeSubmixes;
    size_t m_5msFrames = 0;
    std::function<void(double dt)> m_5msCallback;

    /* Shared scratch buffers for accumulating audio data for resampling */
    std::vector<int16_t> m_scratchIn;
    std::vector<int16_t> m_scratch16Pre;
    std::vector<int32_t> m_scratch32Pre;
    std::vector<float> m_scratchFltPre;
    std::vector<int16_t> m_scratch16Post;
    std::vector<int32_t> m_scratch32Post;
    std::vector<float> m_scratchFltPost;

    AudioSubmix m_mainSubmix;
    std::list<AudioSubmix*> m_linearizedSubmixes;
    bool m_submixesDirty = true;

    void _pumpAndMixVoices(size_t frames, int16_t* dataOut);
    void _pumpAndMixVoices(size_t frames, int32_t* dataOut);
    void _pumpAndMixVoices(size_t frames, float* dataOut);

    void _unbindFrom(std::list<AudioVoice*>::iterator it);
    void _unbindFrom(std::list<AudioSubmix*>::iterator it);

public:
    BaseAudioVoiceEngine() : m_mainSubmix(*this, nullptr, -1, false) {}
    ~BaseAudioVoiceEngine();
    std::unique_ptr<IAudioVoice> allocateNewMonoVoice(double sampleRate,
                                                      IAudioVoiceCallback* cb,
                                                      bool dynamicPitch=false);

    std::unique_ptr<IAudioVoice> allocateNewStereoVoice(double sampleRate,
                                                        IAudioVoiceCallback* cb,
                                                        bool dynamicPitch=false);

    std::unique_ptr<IAudioSubmix> allocateNewSubmix(bool mainOut, IAudioSubmixCallback* cb, int busId);

    void register5MsCallback(std::function<void(double dt)>&& callback);

    void setVolume(float vol);
    const AudioVoiceEngineMixInfo& mixInfo() const;
    AudioChannelSet getAvailableSet() {return m_mixInfo.m_channels;}
    void pumpAndMixVoices() {}
    size_t get5MsFrames() const {return m_5msFrames;}
};

}

#endif // BOO_AUDIOVOICEENGINE_HPP
