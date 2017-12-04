#ifndef BOO_AUDIOVOICE_HPP
#define BOO_AUDIOVOICE_HPP

#include <soxr.h>
#include <list>
#include <unordered_map>
#include "boo/audiodev/IAudioVoice.hpp"
#include "AudioMatrix.hpp"
#include "Common.hpp"
#include "AudioVoiceEngine.hpp"

struct AudioUnitVoiceEngine;
struct VSTVoiceEngine;
struct WAVOutVoiceEngine;

namespace boo
{
class BaseAudioVoiceEngine;
struct AudioVoiceEngineMixInfo;
struct IAudioSubmix;

class AudioVoice : public ListNode<AudioVoice, BaseAudioVoiceEngine*, IAudioVoice>
{
    friend class BaseAudioVoiceEngine;
    friend class AudioSubmix;
    friend struct WASAPIAudioVoiceEngine;
    friend struct ::AudioUnitVoiceEngine;
    friend struct ::VSTVoiceEngine;
    friend struct ::WAVOutVoiceEngine;

protected:
    /* Callback (audio source) */
    IAudioVoiceCallback* m_cb;

    /* Sample-rate converter */
    soxr_t m_src = nullptr;
    double m_sampleRateIn;
    double m_sampleRateOut;
    bool m_dynamicRate;

    /* Running bool */
    bool m_running = false;

    /* Deferred sample-rate reset */
    bool m_resetSampleRate = false;
    double m_deferredSampleRate;
    virtual void _resetSampleRate(double sampleRate)=0;

    /* Deferred pitch ratio set */
    bool m_setPitchRatio = false;
    double m_pitchRatio = 1.0;
    bool m_slew = false;
    void _setPitchRatio(double ratio, bool slew);

    /* Mid-pump update */
    void _midUpdate();

    virtual size_t pumpAndMix16(size_t frames)=0;
    virtual size_t pumpAndMix32(size_t frames)=0;
    virtual size_t pumpAndMixFlt(size_t frames)=0;
    template <typename T> size_t pumpAndMix(size_t frames);

    AudioVoice(BaseAudioVoiceEngine& root, IAudioVoiceCallback* cb, bool dynamicRate);

public:
    static AudioVoice*& _getHeadPtr(BaseAudioVoiceEngine* head);
    static std::unique_lock<std::recursive_mutex> _getHeadLock(BaseAudioVoiceEngine* head);
    std::unique_lock<std::recursive_mutex> destructorLock();

    ~AudioVoice();
    void resetSampleRate(double sampleRate);
    void setPitchRatio(double ratio, bool slew);
    void start();
    void stop();
    double getSampleRateIn() const {return m_sampleRateIn;}
    double getSampleRateOut() const {return m_sampleRateOut;}
};

template <> inline size_t AudioVoice::pumpAndMix<int16_t>(size_t frames) { return pumpAndMix16(frames); }
template <> inline size_t AudioVoice::pumpAndMix<int32_t>(size_t frames) { return pumpAndMix32(frames); }
template <> inline size_t AudioVoice::pumpAndMix<float>(size_t frames) { return pumpAndMixFlt(frames); }

class AudioVoiceMono : public AudioVoice
{
    std::unordered_map<IAudioSubmix*, AudioMatrixMono> m_sendMatrices;
    bool m_silentOut = false;
    void _resetSampleRate(double sampleRate);

    static size_t SRCCallback(AudioVoiceMono* ctx,
                              int16_t** data, size_t requestedLen);

    bool isSilent() const;

    template <typename T> size_t _pumpAndMix(size_t frames);
    size_t pumpAndMix16(size_t frames) { return _pumpAndMix<int16_t>(frames); }
    size_t pumpAndMix32(size_t frames) { return _pumpAndMix<int32_t>(frames); }
    size_t pumpAndMixFlt(size_t frames) { return _pumpAndMix<float>(frames); }

public:
    AudioVoiceMono(BaseAudioVoiceEngine& root, IAudioVoiceCallback* cb,
                   double sampleRate, bool dynamicRate);
    void resetChannelLevels();
    void setMonoChannelLevels(IAudioSubmix* submix, const float coefs[8], bool slew);
    void setStereoChannelLevels(IAudioSubmix* submix, const float coefs[8][2], bool slew);
};

class AudioVoiceStereo : public AudioVoice
{
    std::unordered_map<IAudioSubmix*, AudioMatrixStereo> m_sendMatrices;
    bool m_silentOut = false;
    void _resetSampleRate(double sampleRate);

    static size_t SRCCallback(AudioVoiceStereo* ctx,
                              int16_t** data, size_t requestedLen);

    bool isSilent() const;

    template <typename T> size_t _pumpAndMix(size_t frames);
    size_t pumpAndMix16(size_t frames) { return _pumpAndMix<int16_t>(frames); }
    size_t pumpAndMix32(size_t frames) { return _pumpAndMix<int32_t>(frames); }
    size_t pumpAndMixFlt(size_t frames) { return _pumpAndMix<float>(frames); }

public:
    AudioVoiceStereo(BaseAudioVoiceEngine& root, IAudioVoiceCallback* cb,
                     double sampleRate, bool dynamicRate);
    void resetChannelLevels();
    void setMonoChannelLevels(IAudioSubmix* submix, const float coefs[8], bool slew);
    void setStereoChannelLevels(IAudioSubmix* submix, const float coefs[8][2], bool slew);
};

}

#endif // BOO_AUDIOVOICE_HPP
