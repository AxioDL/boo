#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include "boo/BooObject.hpp"

namespace boo
{
struct IAudioVoice;
struct IAudioVoiceCallback;
struct ChannelMap;
struct IAudioSubmixCallback;

enum class SubmixFormat
{
    Int16,
    Int32,
    Float
};

struct IAudioSubmix : IObj
{
    /** Reset channel-levels to silence; unbind all submixes */
    virtual void resetSendLevels()=0;

    /** Set channel-levels for target submix (AudioChannel enum for array index) */
    virtual void setSendLevel(IAudioSubmix* submix, float level, bool slew)=0;

    /** Gets fixed sample rate of submix this way */
    virtual double getSampleRate() const=0;

    /** Gets fixed sample format of submix this way */
    virtual SubmixFormat getSampleFormat() const=0;
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

    /** Notify of output sample rate changes (for instance, changing the default audio device on Windows) */
    virtual void resetOutputSampleRate(double sampleRate)=0;
};

}

