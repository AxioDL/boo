#ifndef BOO_AUDIOSUBMIX_HPP
#define BOO_AUDIOSUBMIX_HPP

#include "boo/audiodev/IAudioSubmix.hpp"
#include "IAudioMix.hpp"
#include <list>
#include <vector>

struct AudioUnitVoiceEngine;
struct VSTVoiceEngine;
struct WAVOutVoiceEngine;

namespace boo
{
class BaseAudioVoiceEngine;
class AudioVoice;

class AudioSubmix : public IAudioSubmix, public IAudioMix
{
    friend class BaseAudioVoiceEngine;
    friend struct WASAPIAudioVoiceEngine;
    friend struct ::AudioUnitVoiceEngine;
    friend struct ::VSTVoiceEngine;
    friend struct ::WAVOutVoiceEngine;

    /* Mixer-engine relationships */
    BaseAudioVoiceEngine& m_root;
    IAudioMix& m_parent;
    std::list<AudioSubmix*>::iterator m_parentIt;
    bool m_bound = false;
    void bindSubmix(std::list<AudioSubmix*>::iterator pIt)
    {
        m_bound = true;
        m_parentIt = pIt;
    }

    /* Callback (effect source, optional) */
    IAudioSubmixCallback* m_cb;

    /* Audio sources */
    std::list<AudioVoice*> m_activeVoices;
    std::list<AudioSubmix*> m_activeSubmixes;

    /* Output gains for each channel */
    float m_gains[8];

    /* Temporary scratch buffers for accumulating submix audio */
    std::vector<int16_t> m_scratch16;
    std::vector<int32_t> m_scratch32;
    std::vector<float> m_scratchFlt;

    void _pumpAndMixVoices(size_t frames, int16_t* dataOut, int16_t* mainOut);
    void _pumpAndMixVoices(size_t frames, int32_t* dataOut, int32_t* mainOut);
    void _pumpAndMixVoices(size_t frames, float* dataOut, float* mainOut);

    void _unbindFrom(std::list<AudioVoice*>::iterator it);
    void _unbindFrom(std::list<AudioSubmix*>::iterator it);

    void _resetOutputSampleRate();

public:
    ~AudioSubmix();
    AudioSubmix(BaseAudioVoiceEngine& root, IAudioMix& parent, IAudioSubmixCallback* cb);

    std::unique_ptr<IAudioVoice> allocateNewMonoVoice(double sampleRate,
                                                      IAudioVoiceCallback* cb,
                                                      bool dynamicPitch=false);

    std::unique_ptr<IAudioVoice> allocateNewStereoVoice(double sampleRate,
                                                        IAudioVoiceCallback* cb,
                                                        bool dynamicPitch=false);

    std::unique_ptr<IAudioSubmix> allocateNewSubmix(IAudioSubmixCallback* cb=nullptr);
    void setChannelGains(const float gains[8]);
    void unbindSubmix();
    const AudioVoiceEngineMixInfo& mixInfo() const;
    double getSampleRate() const;
    SubmixFormat getSampleFormat() const;
};

}

#endif // BOO_AUDIOSUBMIX_HPP
