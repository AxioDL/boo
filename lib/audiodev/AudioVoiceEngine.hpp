#ifndef BOO_AUDIOVOICEENGINE_HPP
#define BOO_AUDIOVOICEENGINE_HPP

#include "boo/audiodev/IAudioVoiceEngine.hpp"
#include "LtRtProcessing.hpp"
#include "Common.hpp"
#include "AudioVoice.hpp"
#include "AudioSubmix.hpp"
#include <functional>

namespace boo
{

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
    IAudioVoiceEngineCallback* m_engineCallback = nullptr;

    /* Shared scratch buffers for accumulating audio data for resampling */
    std::vector<int16_t> m_scratchIn;
    std::vector<int16_t> m_scratch16Pre;
    std::vector<int32_t> m_scratch32Pre;
    std::vector<float> m_scratchFltPre;
    std::vector<int16_t> m_scratch16Post;
    std::vector<int32_t> m_scratch32Post;
    std::vector<float> m_scratchFltPost;

    /* LtRt processing if enabled */
    std::unique_ptr<LtRtProcessing> m_ltRtProcessing;
    std::vector<int16_t> m_ltRtIn16;
    std::vector<int32_t> m_ltRtIn32;
    std::vector<float> m_ltRtInFlt;

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

    void setCallbackInterface(IAudioVoiceEngineCallback* cb);

    void setVolume(float vol);
    bool enableLtRt(bool enable);
    const AudioVoiceEngineMixInfo& mixInfo() const;
    const AudioVoiceEngineMixInfo& clientMixInfo() const;
    AudioChannelSet getAvailableSet() {return clientMixInfo().m_channels;}
    void pumpAndMixVoices() {}
    size_t get5MsFrames() const {return m_5msFrames;}
};

}

#endif // BOO_AUDIOVOICEENGINE_HPP
