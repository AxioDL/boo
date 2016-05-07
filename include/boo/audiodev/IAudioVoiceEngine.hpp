#ifndef BOO_IAUDIOVOICEENGINE_HPP
#define BOO_IAUDIOVOICEENGINE_HPP

#include "IAudioVoice.hpp"
#include "IAudioSubmix.hpp"
#include <memory>

namespace boo
{

/** Mixing and sample-rate-conversion system. Allocates voices and mixes them
 *  before sending the final samples to an OS-supplied audio-queue */
struct IAudioVoiceEngine
{
    virtual ~IAudioVoiceEngine() = default;

    /** Client calls this to request allocation of new mixer-voice.
     *  Returns empty unique_ptr if necessary resources aren't available.
     *  ChannelLayout automatically reduces to maximum-supported layout by HW.
     *
     *  Client must be prepared to supply audio frames via the callback when this is called;
     *  the backing audio-buffers are primed with initial data for low-latency playback start
     */
    virtual std::unique_ptr<IAudioVoice> allocateNewMonoVoice(double sampleRate,
                                                              IAudioVoiceCallback* cb,
                                                              bool dynamicPitch=false)=0;

    /** Same as allocateNewMonoVoice, but source audio is stereo-interleaved */
    virtual std::unique_ptr<IAudioVoice> allocateNewStereoVoice(double sampleRate,
                                                                IAudioVoiceCallback* cb,
                                                                bool dynamicPitch=false)=0;

    /** Client calls this to allocate a Submix for gathering audio together for effects processing */
    virtual std::unique_ptr<IAudioSubmix> allocateNewSubmix(IAudioSubmixCallback* cb=nullptr)=0;

    /** Client may use this to determine current speaker-setup */
    virtual AudioChannelSet getAvailableSet()=0;

    /** Ensure backing platform buffer is filled as much as possible with mixed samples */
    virtual void pumpAndMixVoices()=0;
};

/** Construct host platform's voice engine */
std::unique_ptr<IAudioVoiceEngine> NewAudioVoiceEngine();

}

#endif // BOO_IAUDIOVOICEENGINE_HPP
