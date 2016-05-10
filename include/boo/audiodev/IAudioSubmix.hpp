#ifndef BOO_IAUDIOSUBMIX_HPP
#define BOO_IAUDIOSUBMIX_HPP

#include <stddef.h>
#include <stdint.h>
#include <memory>

namespace boo
{
class IAudioVoice;
class IAudioVoiceCallback;
struct ChannelMap;
struct IAudioSubmixCallback;

struct IAudioSubmix
{
    virtual ~IAudioSubmix() = default;

    /** Same as the IAudioVoice allocator, but produces audio within the submix */
    virtual std::unique_ptr<IAudioVoice> allocateNewMonoVoice(double sampleRate,
                                                              IAudioVoiceCallback* cb,
                                                              bool dynamicPitch=false)=0;

    /** Same as allocateNewMonoVoice, but source audio is stereo-interleaved */
    virtual std::unique_ptr<IAudioVoice> allocateNewStereoVoice(double sampleRate,
                                                                IAudioVoiceCallback* cb,
                                                                bool dynamicPitch=false)=0;

    /** Same as the IAudioVoice allocator, but produces audio recursively within the submix */
    virtual std::unique_ptr<IAudioSubmix> allocateNewSubmix(IAudioSubmixCallback* cb=nullptr)=0;

    /** Sets gain factors for each channel once accumulated by the submix */
    virtual void setChannelGains(const float gains[8])=0;
};

struct IAudioSubmixCallback
{
    /** Client-provided claim to implement / is ready to call applyEffect() */
    virtual bool canApplyEffect() const=0;

    /** Client-provided effect solution for interleaved, master sample-rate audio */
    virtual void applyEffect(int16_t* audio, size_t frameCount,
                             const ChannelMap& chanMap, double sampleRate) const=0;
    virtual void applyEffect(int32_t* audio, size_t frameCount,
                             const ChannelMap& chanMap, double sampleRate) const=0;
    virtual void applyEffect(float* audio, size_t frameCount,
                             const ChannelMap& chanMap, double sampleRate) const=0;
};

}

#endif // BOO_IAUDIOVOICE_HPP
