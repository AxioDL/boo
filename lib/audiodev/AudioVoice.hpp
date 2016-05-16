#ifndef BOO_AUDIOVOICE_HPP
#define BOO_AUDIOVOICE_HPP

#include <soxr.h>
#include <list>
#include "boo/audiodev/IAudioVoice.hpp"
#include "AudioMatrix.hpp"

namespace boo
{
class BaseAudioVoiceEngine;
struct AudioVoiceEngineMixInfo;
class IAudioMix;

class AudioVoice : public IAudioVoice
{
    friend class BaseAudioVoiceEngine;
    friend class AudioSubmix;

protected:
    /* Mixer-engine relationships */
    BaseAudioVoiceEngine& m_root;
    IAudioMix& m_parent;
    std::list<AudioVoice*>::iterator m_parentIt;
    bool m_bound = false;
    void bindVoice(std::list<AudioVoice*>::iterator pIt)
    {
        m_bound = true;
        m_parentIt = pIt;
    }

    /* Callback (audio source) */
    IAudioVoiceCallback* m_cb;

    /* Sample-rate converter */
    soxr_t m_src = nullptr;
    double m_sampleRateIn;
    double m_sampleRateOut;
    size_t m_5msFrames;
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
    void _setPitchRatio(double ratio);

    /* Mid-pump update */
    void _midUpdate();

    virtual size_t pumpAndMix(const AudioVoiceEngineMixInfo& mixInfo, size_t frames, int16_t* buf)=0;
    virtual size_t pumpAndMix(const AudioVoiceEngineMixInfo& mixInfo, size_t frames, int32_t* buf)=0;
    virtual size_t pumpAndMix(const AudioVoiceEngineMixInfo& mixInfo, size_t frames, float* buf)=0;
    AudioVoice(BaseAudioVoiceEngine& root, IAudioMix& parent, IAudioVoiceCallback* cb, bool dynamicRate);

public:
    ~AudioVoice();
    void resetSampleRate(double sampleRate);
    void setPitchRatio(double ratio);
    void start();
    void stop();
    void unbindVoice();
};

class AudioVoiceMono : public AudioVoice
{
    AudioMatrixMono m_matrix;
    void _resetSampleRate(double sampleRate);

    static size_t SRCCallback(AudioVoiceMono* ctx,
                              int16_t** data, size_t requestedLen);

    size_t pumpAndMix(const AudioVoiceEngineMixInfo& mixInfo, size_t frames, int16_t* buf);
    size_t pumpAndMix(const AudioVoiceEngineMixInfo& mixInfo, size_t frames, int32_t* buf);
    size_t pumpAndMix(const AudioVoiceEngineMixInfo& mixInfo, size_t frames, float* buf);

public:
    AudioVoiceMono(BaseAudioVoiceEngine& root, IAudioMix& parent, IAudioVoiceCallback* cb,
                   double sampleRate, bool dynamicRate);
    void setDefaultMatrixCoefficients();
    void setMonoMatrixCoefficients(const float coefs[8]);
    void setStereoMatrixCoefficients(const float coefs[8][2]);
};

class AudioVoiceStereo : public AudioVoice
{
    AudioMatrixStereo m_matrix;
    void _resetSampleRate(double sampleRate);

    static size_t SRCCallback(AudioVoiceStereo* ctx,
                              int16_t** data, size_t requestedLen);

    size_t pumpAndMix(const AudioVoiceEngineMixInfo& mixInfo, size_t frames, int16_t* buf);
    size_t pumpAndMix(const AudioVoiceEngineMixInfo& mixInfo, size_t frames, int32_t* buf);
    size_t pumpAndMix(const AudioVoiceEngineMixInfo& mixInfo, size_t frames, float* buf);

public:
    AudioVoiceStereo(BaseAudioVoiceEngine& root, IAudioMix& parent, IAudioVoiceCallback* cb,
                     double sampleRate, bool dynamicRate);
    void setDefaultMatrixCoefficients();
    void setMonoMatrixCoefficients(const float coefs[8]);
    void setStereoMatrixCoefficients(const float coefs[8][2]);
};

}

#endif // BOO_AUDIOVOICE_HPP
