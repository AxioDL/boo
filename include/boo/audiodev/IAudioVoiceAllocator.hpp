#ifndef BOO_IAUDIOVOICEALLOCATOR_HPP
#define BOO_IAUDIOVOICEALLOCATOR_HPP

#include "IAudioVoice.hpp"
#include <memory>

namespace boo
{

struct IAudioVoiceCallback
{
    /** boo calls this on behalf of the audio platform to request more audio
     *  frames from the client */
    virtual void needsNextBuffer(IAudioVoice& voice, size_t frames)=0;
};

struct IAudioVoiceAllocator
{
    virtual ~IAudioVoiceAllocator() = default;

    /** Client calls this to request allocation of new mixer-voice.
     *  Returns empty unique_ptr if necessary resources aren't available.
     *  ChannelLayout automatically reduces to maximum-supported layout by HW.
     *
     *  Client must be prepared to supply audio frames via the callback when this is called;
     *  the backing audio-buffers are primed with initial data for low-latency playback start */
    virtual std::unique_ptr<IAudioVoice> allocateNewVoice(AudioChannelSet layoutOut,
                                                          unsigned sampleRate,
                                                          IAudioVoiceCallback* cb)=0;

    /** Client may use this to determine current speaker-setup */
    virtual AudioChannelSet getAvailableSet()=0;

    /** Ensure all voices' platform buffers are filled as much as possible */
    virtual void pumpVoices()=0;
};

/** Obtain host platform's voice allocator */
std::unique_ptr<IAudioVoiceAllocator> NewAudioVoiceAllocator();

}

#endif // BOO_IAUDIOVOICEALLOCATOR_HPP
