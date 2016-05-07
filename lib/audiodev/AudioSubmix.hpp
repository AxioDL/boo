#ifndef BOO_AUDIOSUBMIX_HPP
#define BOO_AUDIOSUBMIX_HPP

#include "boo/audiodev/IAudioSubmix.hpp"
#include "IAudioHost.hpp"
#include <list>

namespace boo
{
class BaseAudioVoiceEngine;
class AudioVoice;

class AudioSubmix : public IAudioSubmix, public IAudioHost
{
    friend class BaseAudioVoiceEngine;

    /* Mixer-engine relationships */
    IAudioHost& m_parent;
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

    void _pumpAndMixVoices(size_t frames, int16_t* dataOut);
    void _pumpAndMixVoices(size_t frames, int32_t* dataOut);
    void _pumpAndMixVoices(size_t frames, float* dataOut);

    void _unbindFrom(std::list<AudioVoice*>::iterator it);
    void _unbindFrom(std::list<AudioSubmix*>::iterator it);

public:
    ~AudioSubmix();
    AudioSubmix(IAudioHost& parent, IAudioSubmixCallback* cb);

    std::unique_ptr<IAudioVoice> allocateNewMonoVoice(double sampleRate,
                                                      IAudioVoiceCallback* cb,
                                                      bool dynamicPitch=false);

    std::unique_ptr<IAudioVoice> allocateNewStereoVoice(double sampleRate,
                                                        IAudioVoiceCallback* cb,
                                                        bool dynamicPitch=false);

    virtual std::unique_ptr<IAudioSubmix> allocateNewSubmix(IAudioSubmixCallback* cb=nullptr);

    void unbindSubmix();

    const AudioVoiceEngineMixInfo& mixInfo() const;
};

}

#endif // BOO_AUDIOSUBMIX_HPP
