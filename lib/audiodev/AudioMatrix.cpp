#include "AudioMatrix.hpp"
#include "AudioVoiceEngine.hpp"
#include <string.h>

namespace boo
{

void AudioMatrixMono::setDefaultMatrixCoefficients(AudioChannelSet acSet)
{
    m_curSlewFrame = 0;
    m_slewFrames = 0;
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

int16_t* AudioMatrixMono::mixMonoSampleData(const AudioVoiceEngineMixInfo& info,
                                            const int16_t* dataIn, int16_t* dataOut, size_t samples)
{
    const ChannelMap& chmap = info.m_channelMap;
    for (size_t s=0 ; s<samples ; ++s, ++dataIn)
    {
        if (m_slewFrames && m_curSlewFrame < m_slewFrames)
        {
            double t = m_curSlewFrame / double(m_slewFrames);
            double omt = 1.0 - t;

            for (unsigned c=0 ; c<chmap.m_channelCount ; ++c)
            {
                AudioChannel ch = chmap.m_channels[c];
                if (ch != AudioChannel::Unknown)
                {
                    *dataOut = Clamp16(*dataOut + *dataIn * (m_coefs[int(ch)] * t + m_oldCoefs[int(ch)] * omt));
                    ++dataOut;
                }
            }

            ++m_curSlewFrame;
        }
        else
        {
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
    }
    return dataOut;
}

int32_t* AudioMatrixMono::mixMonoSampleData(const AudioVoiceEngineMixInfo& info,
                                            const int32_t* dataIn, int32_t* dataOut, size_t samples)
{
    const ChannelMap& chmap = info.m_channelMap;
    for (size_t s=0 ; s<samples ; ++s, ++dataIn)
    {
        if (m_slewFrames && m_curSlewFrame < m_slewFrames)
        {
            double t = m_curSlewFrame / double(m_slewFrames);
            double omt = 1.0 - t;

            for (unsigned c=0 ; c<chmap.m_channelCount ; ++c)
            {
                AudioChannel ch = chmap.m_channels[c];
                if (ch != AudioChannel::Unknown)
                {
                    *dataOut = Clamp32(*dataOut + *dataIn * (m_coefs[int(ch)] * t + m_oldCoefs[int(ch)] * omt));
                    ++dataOut;
                }
            }

            ++m_curSlewFrame;
        }
        else
        {
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
    }
    return dataOut;
}

float* AudioMatrixMono::mixMonoSampleData(const AudioVoiceEngineMixInfo& info,
                                          const float* dataIn, float* dataOut, size_t samples)
{
    const ChannelMap& chmap = info.m_channelMap;
    for (size_t s=0 ; s<samples ; ++s, ++dataIn)
    {
        if (m_slewFrames && m_curSlewFrame < m_slewFrames)
        {
            double t = m_curSlewFrame / double(m_slewFrames);
            double omt = 1.0 - t;

            for (unsigned c=0 ; c<chmap.m_channelCount ; ++c)
            {
                AudioChannel ch = chmap.m_channels[c];
                if (ch != AudioChannel::Unknown)
                {
                    *dataOut = ClampFlt(*dataOut + *dataIn * (m_coefs[int(ch)] * t + m_oldCoefs[int(ch)] * omt));
                    ++dataOut;
                }
            }

            ++m_curSlewFrame;
        }
        else
        {
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
    }
    return dataOut;
}

void AudioMatrixStereo::setDefaultMatrixCoefficients(AudioChannelSet acSet)
{
    m_curSlewFrame = 0;
    m_slewFrames = 0;
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

int16_t* AudioMatrixStereo::mixStereoSampleData(const AudioVoiceEngineMixInfo& info,
                                                const int16_t* dataIn, int16_t* dataOut, size_t frames)
{
    const ChannelMap& chmap = info.m_channelMap;
    for (size_t f=0 ; f<frames ; ++f, dataIn += 2)
    {
        if (m_slewFrames && m_curSlewFrame < m_slewFrames)
        {
            double t = m_curSlewFrame / double(m_slewFrames);
            double omt = 1.0 - t;

            for (unsigned c=0 ; c<chmap.m_channelCount ; ++c)
            {
                AudioChannel ch = chmap.m_channels[c];
                if (ch != AudioChannel::Unknown)
                {
                    *dataOut = Clamp16(*dataOut +
                                       *dataIn * (m_coefs[int(ch)][0] * t + m_oldCoefs[int(ch)][0] * omt) +
                                       *dataIn * (m_coefs[int(ch)][1] * t + m_oldCoefs[int(ch)][1] * omt));
                    ++dataOut;
                }
            }

            ++m_curSlewFrame;
        }
        else
        {
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
    }
    return dataOut;
}

int32_t* AudioMatrixStereo::mixStereoSampleData(const AudioVoiceEngineMixInfo& info,
                                                const int32_t* dataIn, int32_t* dataOut, size_t frames)
{
    const ChannelMap& chmap = info.m_channelMap;
    for (size_t f=0 ; f<frames ; ++f, dataIn += 2)
    {
        if (m_slewFrames && m_curSlewFrame < m_slewFrames)
        {
            double t = m_curSlewFrame / double(m_slewFrames);
            double omt = 1.0 - t;

            for (unsigned c=0 ; c<chmap.m_channelCount ; ++c)
            {
                AudioChannel ch = chmap.m_channels[c];
                if (ch != AudioChannel::Unknown)
                {
                    *dataOut = Clamp32(*dataOut +
                                       *dataIn * (m_coefs[int(ch)][0] * t + m_oldCoefs[int(ch)][0] * omt) +
                                       *dataIn * (m_coefs[int(ch)][1] * t + m_oldCoefs[int(ch)][1] * omt));
                    ++dataOut;
                }
            }

            ++m_curSlewFrame;
        }
        else
        {
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
    }
    return dataOut;
}

float* AudioMatrixStereo::mixStereoSampleData(const AudioVoiceEngineMixInfo& info,
                                              const float* dataIn, float* dataOut, size_t frames)
{
    const ChannelMap& chmap = info.m_channelMap;
    for (size_t f=0 ; f<frames ; ++f, dataIn += 2)
    {
        if (m_slewFrames && m_curSlewFrame < m_slewFrames)
        {
            double t = m_curSlewFrame / double(m_slewFrames);
            double omt = 1.0 - t;

            for (unsigned c=0 ; c<chmap.m_channelCount ; ++c)
            {
                AudioChannel ch = chmap.m_channels[c];
                if (ch != AudioChannel::Unknown)
                {
                    *dataOut = ClampFlt(*dataOut +
                                        *dataIn * (m_coefs[int(ch)][0] * t + m_oldCoefs[int(ch)][0] * omt) +
                                        *dataIn * (m_coefs[int(ch)][1] * t + m_oldCoefs[int(ch)][1] * omt));
                    ++dataOut;
                }
            }

            ++m_curSlewFrame;
        }
        else
        {
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
    return dataOut;
}

}

