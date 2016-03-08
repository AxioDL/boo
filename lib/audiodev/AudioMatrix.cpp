#include "boo/audiodev/AudioMatrix.hpp"
#include <string.h>

namespace boo
{

void AudioMatrixMono::setDefaultMatrixCoefficients()
{
    memset(m_coefs, 0, sizeof(m_coefs));
    switch (m_setOut)
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

void AudioMatrixMono::bufferMonoSampleData(IAudioVoice& voice, const int16_t* data, size_t samples)
{
    const ChannelMap& chmap = voice.channelMap();
    m_interleaveBuf.clear();
    m_interleaveBuf.reserve(samples * chmap.m_channelCount);
    for (size_t s=0 ; s<samples ; ++s, ++data)
        for (int c=0 ; c<chmap.m_channelCount ; ++c)
        {
            AudioChannel ch = chmap.m_channels[c];
            if (ch == AudioChannel::Unknown)
                m_interleaveBuf.push_back(0);
            else
                m_interleaveBuf.push_back(data[0] * m_coefs[int(ch)]);
        }
    voice.bufferSampleData(m_interleaveBuf.data(), samples);
}

void AudioMatrixStereo::setDefaultMatrixCoefficients()
{
    memset(m_coefs, 0, sizeof(m_coefs));
    switch (m_setOut)
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

void AudioMatrixStereo::bufferStereoSampleData(IAudioVoice& voice, const int16_t* data, size_t frames)
{
    const ChannelMap& chmap = voice.channelMap();
    m_interleaveBuf.clear();
    m_interleaveBuf.reserve(frames * chmap.m_channelCount);
    for (size_t f=0 ; f<frames ; ++f, data += 2)
        for (int c=0 ; c<chmap.m_channelCount ; ++c)
        {
            AudioChannel ch = chmap.m_channels[c];
            if (ch == AudioChannel::Unknown)
                m_interleaveBuf.push_back(0);
            else
                m_interleaveBuf.push_back(data[0] * m_coefs[int(ch)][0] +
                                          data[1] * m_coefs[int(ch)][1]);
        }
    voice.bufferSampleData(m_interleaveBuf.data(), frames);
}

}

