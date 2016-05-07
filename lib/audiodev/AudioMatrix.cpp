#include "AudioMatrix.hpp"
#include "AudioVoiceEngine.hpp"
#include <string.h>

namespace boo
{

void AudioMatrixMono::setDefaultMatrixCoefficients(AudioChannelSet acSet)
{
    memset(m_coefs, 0, sizeof(m_coefs));
    switch (acSet)
    {
    case AudioChannelSet::Stereo:
    case AudioChannelSet::Quad:
        m_coefs[int(AudioChannel::FrontLeft)] = 1.0;
        m_coefs[int(AudioChannel::FrontRight)] = 1.0;
        break;
    case AudioChannelSet::Surround51:
    case AudioChannelSet::Surround71:
        m_coefs[int(AudioChannel::FrontCenter)] = 1.0;
        break;
    default: break;
    }
}

void AudioMatrixMono::mixMonoSampleData(const AudioVoiceEngineMixInfo& info,
                                        const int16_t* dataIn, int16_t* dataOut, size_t samples) const
{
    const ChannelMap& chmap = info.m_channelMap;
    for (size_t s=0 ; s<samples ; ++s, ++dataIn)
        for (unsigned c=0 ; c<chmap.m_channelCount ; ++c)
        {
            AudioChannel ch = chmap.m_channels[c];
            if (ch != AudioChannel::Unknown)
            {
                *dataOut = Clamp16(*dataOut + *dataIn * m_coefs[int(ch)]);
                ++dataOut;
            }
        }
}

void AudioMatrixMono::mixMonoSampleData(const AudioVoiceEngineMixInfo& info,
                                        const int32_t* dataIn, int32_t* dataOut, size_t samples) const
{
    const ChannelMap& chmap = info.m_channelMap;
    for (size_t s=0 ; s<samples ; ++s, ++dataIn)
        for (unsigned c=0 ; c<chmap.m_channelCount ; ++c)
        {
            AudioChannel ch = chmap.m_channels[c];
            if (ch != AudioChannel::Unknown)
            {
                *dataOut = Clamp32(*dataOut + *dataIn * m_coefs[int(ch)]);
                ++dataOut;
            }
        }
}

void AudioMatrixMono::mixMonoSampleData(const AudioVoiceEngineMixInfo& info,
                                        const float* dataIn, float* dataOut, size_t samples) const
{
    const ChannelMap& chmap = info.m_channelMap;
    for (size_t s=0 ; s<samples ; ++s, ++dataIn)
        for (unsigned c=0 ; c<chmap.m_channelCount ; ++c)
        {
            AudioChannel ch = chmap.m_channels[c];
            if (ch != AudioChannel::Unknown)
            {
                *dataOut = ClampFlt(*dataOut + *dataIn * m_coefs[int(ch)]);
                ++dataOut;
            }
        }
}

void AudioMatrixStereo::setDefaultMatrixCoefficients(AudioChannelSet acSet)
{
    memset(m_coefs, 0, sizeof(m_coefs));
    switch (acSet)
    {
    case AudioChannelSet::Stereo:
    case AudioChannelSet::Quad:
        m_coefs[int(AudioChannel::FrontLeft)][0] = 1.0;
        m_coefs[int(AudioChannel::FrontRight)][1] = 1.0;
        break;
    case AudioChannelSet::Surround51:
    case AudioChannelSet::Surround71:
        m_coefs[int(AudioChannel::FrontLeft)][0] = 1.0;
        m_coefs[int(AudioChannel::FrontRight)][1] = 1.0;
        break;
    default: break;
    }
}

void AudioMatrixStereo::mixStereoSampleData(const AudioVoiceEngineMixInfo& info,
                                            const int16_t* dataIn, int16_t* dataOut, size_t frames) const
{
    const ChannelMap& chmap = info.m_channelMap;
    for (size_t f=0 ; f<frames ; ++f, dataIn += 2)
        for (unsigned c=0 ; c<chmap.m_channelCount ; ++c)
        {
            AudioChannel ch = chmap.m_channels[c];
            if (ch != AudioChannel::Unknown)
            {
                *dataOut = Clamp16(*dataOut +
                                   dataIn[0] * m_coefs[int(ch)][0] +
                                   dataIn[1] * m_coefs[int(ch)][1]);
                ++dataOut;
            }
        }
}

void AudioMatrixStereo::mixStereoSampleData(const AudioVoiceEngineMixInfo& info,
                                            const int32_t* dataIn, int32_t* dataOut, size_t frames) const
{
    const ChannelMap& chmap = info.m_channelMap;
    for (size_t f=0 ; f<frames ; ++f, dataIn += 2)
        for (unsigned c=0 ; c<chmap.m_channelCount ; ++c)
        {
            AudioChannel ch = chmap.m_channels[c];
            if (ch != AudioChannel::Unknown)
            {
                *dataOut = Clamp32(*dataOut +
                                   dataIn[0] * m_coefs[int(ch)][0] +
                                   dataIn[1] * m_coefs[int(ch)][1]);
                ++dataOut;
            }
        }
}

void AudioMatrixStereo::mixStereoSampleData(const AudioVoiceEngineMixInfo& info,
                                            const float* dataIn, float* dataOut, size_t frames) const
{
    const ChannelMap& chmap = info.m_channelMap;
    for (size_t f=0 ; f<frames ; ++f, dataIn += 2)
        for (unsigned c=0 ; c<chmap.m_channelCount ; ++c)
        {
            AudioChannel ch = chmap.m_channels[c];
            if (ch != AudioChannel::Unknown)
            {
                *dataOut = ClampFlt(*dataOut +
                                    dataIn[0] * m_coefs[int(ch)][0] +
                                    dataIn[1] * m_coefs[int(ch)][1]);
                ++dataOut;
            }
        }
}

}

