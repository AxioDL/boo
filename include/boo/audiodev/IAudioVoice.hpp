#ifndef BOO_IAUDIOVOICE_HPP
#define BOO_IAUDIOVOICE_HPP

#include <stddef.h>
#include <stdint.h>

namespace boo
{

enum class AudioChannelSet
{
    Stereo,
    Quad,
    Surround51,
    Surround71,
    Unknown = 0xff
};

enum class AudioChannel
{
    FrontLeft,
    FrontRight,
    RearLeft,
    RearRight,
    FrontCenter,
    LFE,
    SideLeft,
    SideRight,
    Unknown = 0xff
};

struct ChannelMap
{
    unsigned m_channelCount = 0;
    AudioChannel m_channels[8] = {};
};

static inline unsigned ChannelCount(AudioChannelSet layout)
{
    switch (layout)
    {
    case AudioChannelSet::Stereo:
        return 2;
    case AudioChannelSet::Quad:
        return 4;
    case AudioChannelSet::Surround51:
        return 6;
    case AudioChannelSet::Surround71:
        return 8;
    default: break;
    }
    return 0;
}

struct IAudioVoice
{
    virtual ~IAudioVoice() = default;

    /** Reset channel-gains to voice defaults */
    virtual void setDefaultMatrixCoefficients()=0;

    /** Set channel-gains for mono audio source (AudioChannel enum for array index) */
    virtual void setMonoMatrixCoefficients(const float coefs[8])=0;

    /** Set channel-gains for stereo audio source (AudioChannel enum for array index) */
    virtual void setStereoMatrixCoefficients(const float coefs[8][2])=0;

    /** Called by client to dynamically adjust the pitch of voices with dynamic pitch enabled */
    virtual void setPitchRatio(double ratio)=0;

    /** Instructs platform to begin consuming sample data; invoking callback as needed */
    virtual void start()=0;

    /** Instructs platform to stop consuming sample data */
    virtual void stop()=0;

    /** Invalidates this voice by removing it from the AudioVoiceEngine */
    virtual void unbindVoice()=0;
};

struct IAudioVoiceCallback
{
    /** boo calls this on behalf of the audio platform to request more audio
     *  frames from the client */
    virtual size_t supplyAudio(IAudioVoice& voice, size_t frames, int16_t* data)=0;
};

}

#endif // BOO_IAUDIOVOICE_HPP
