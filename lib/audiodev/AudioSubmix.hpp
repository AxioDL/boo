#ifndef BOO_AUDIOSUBMIX_HPP
#define BOO_AUDIOSUBMIX_HPP

#include "boo/audiodev/IAudioSubmix.hpp"
#include <list>
#include <vector>
#include <unordered_map>

#if __SSE__
#include <xmmintrin.h>
#endif

struct AudioUnitVoiceEngine;
struct VSTVoiceEngine;
struct WAVOutVoiceEngine;

namespace boo
{
class BaseAudioVoiceEngine;
class AudioVoice;
struct AudioVoiceEngineMixInfo;
/* Output gains for each mix-send/channel */

class AudioSubmix : public IAudioSubmix
{
    friend class BaseAudioVoiceEngine;
    friend class AudioVoiceMono;
    friend class AudioVoiceStereo;
    friend struct WASAPIAudioVoiceEngine;
    friend struct ::AudioUnitVoiceEngine;
    friend struct ::VSTVoiceEngine;
    friend struct ::WAVOutVoiceEngine;

    /* Mixer-engine relationships */
    BaseAudioVoiceEngine& m_root;
    std::list<AudioSubmix*>::iterator m_parentIt;
    bool m_mainOut;
    bool m_bound = false;
    void bindSubmix(std::list<AudioSubmix*>::iterator pIt)
    {
        m_bound = true;
        m_parentIt = pIt;
    }

    /* Callback (effect source, optional) */
    IAudioSubmixCallback* m_cb;

    /* Slew state for output gains */
    size_t m_slewFrames = 0;
    size_t m_curSlewFrame = 0;

    /* Output gains for each mix-send/channel */
    std::unordered_map<IAudioSubmix*, std::array<float, 2>> m_sendGains;

    /* Temporary scratch buffers for accumulating submix audio */
    std::vector<int16_t> m_scratch16;
    std::vector<int32_t> m_scratch32;
    std::vector<float> m_scratchFlt;

    /* Override scratch buffers with alternate destination */
    int16_t* m_redirect16 = nullptr;
    int32_t* m_redirect32 = nullptr;
    float* m_redirectFlt = nullptr;

    /* C3-linearization support (to mitigate a potential diamond problem on 'clever' submix routes) */
    bool _isDirectDependencyOf(AudioSubmix* send);
    std::list<AudioSubmix*> _linearizeC3();
    static bool _mergeC3(std::list<AudioSubmix*>& output,
                         std::vector<std::list<AudioSubmix*>>& lists);

    /* Fill scratch buffers with silence for new mix cycle */
    void _zeroFill16();
    void _zeroFill32();
    void _zeroFillFlt();

    /* Receive audio from a single voice / submix */
    int16_t* _getMergeBuf16(size_t frames);
    int32_t* _getMergeBuf32(size_t frames);
    float* _getMergeBufFlt(size_t frames);

    /* Mix scratch buffers into sends */
    size_t _pumpAndMix16(size_t frames);
    size_t _pumpAndMix32(size_t frames);
    size_t _pumpAndMixFlt(size_t frames);

    void _resetOutputSampleRate();

public:
    ~AudioSubmix();
    AudioSubmix(BaseAudioVoiceEngine& root, IAudioSubmixCallback* cb, bool mainOut);

    void resetSendLevels();
    void setSendLevel(IAudioSubmix* submix, float level, bool slew);
    void unbindSubmix();
    const AudioVoiceEngineMixInfo& mixInfo() const;
    double getSampleRate() const;
    SubmixFormat getSampleFormat() const;
};

}

#endif // BOO_AUDIOSUBMIX_HPP
