#ifndef BOO_AUDIOVOICE_HPP
#define BOO_AUDIOVOICE_HPP

#include <soxr.h>
#include <list>
#include <unordered_map>
#include "boo/audiodev/IAudioVoice.hpp"
#include "AudioMatrix.hpp"

struct AudioUnitVoiceEngine;
struct VSTVoiceEngine;
struct WAVOutVoiceEngine;

namespace boo
{
class BaseAudioVoiceEngine;
struct AudioVoiceEngineMixInfo;
struct IAudioSubmix;

class AudioVoice : public IAudioVoice
{
    friend class BaseAudioVoiceEngine;
    friend class AudioSubmix;
    friend struct WASAPIAudioVoiceEngine;
    friend struct ::AudioUnitVoiceEngine;
    friend struct ::VSTVoiceEngine;
    friend struct ::WAVOutVoiceEngine;

protected:
    /* Mixer-engine relationships */
    BaseAudioVoiceEngine& m_root;
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

    AudioVoice(BaseAudioVoiceEngine& root, IAudioVoiceCallback* cb, bool dynamicRate);

public:
    ~AudioVoice();
    void resetSampleRate(double sampleRate);
    void setPitchRatio(double ratio, bool slew);
    void start();
    void stop();
    void unbindVoice();
    double getSampleRateIn() const {return m_sampleRateIn;}
    double getSampleRateOut() const {return m_sampleRateOut;}
};

class AudioVoiceMono : public AudioVoice
{
    std::unordered_map<IAudioSubmix*, AudioMatrixMono> m_sendMatrices;
    bool m_silentOut = false;
    void _resetSampleRate(double sampleRate);

    static size_t SRCCallback(AudioVoiceMono* ctx,
                              int16_t** data, size_t requestedLen);

    size_t pumpAndMix16(size_t frames);
    size_t pumpAndMix32(size_t frames);
    size_t pumpAndMixFlt(size_t frames);

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

    size_t pumpAndMix16(size_t frames);
    size_t pumpAndMix32(size_t frames);
    size_t pumpAndMixFlt(size_t frames);

public:
    AudioVoiceStereo(BaseAudioVoiceEngine& root, IAudioVoiceCallback* cb,
                     double sampleRate, bool dynamicRate);
    void resetChannelLevels();
    void setMonoChannelLevels(IAudioSubmix* submix, const float coefs[8], bool slew);
    void setStereoChannelLevels(IAudioSubmix* submix, const float coefs[8][2], bool slew);
};

}

#endif // BOO_AUDIOVOICE_HPP
