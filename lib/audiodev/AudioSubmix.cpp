#include "AudioSubmix.hpp"
#include "AudioVoiceEngine.hpp"
#include "AudioVoice.hpp"
#include <string.h>
#include <algorithm>

namespace boo
{

AudioSubmix::AudioSubmix(BaseAudioVoiceEngine& root, IAudioSubmixCallback* cb, int busId, bool mainOut)
: m_root(root), m_busId(busId), m_cb(cb), m_mainOut(mainOut)
{
    if (mainOut)
        setSendLevel(&m_root.m_mainSubmix, 1.f, false);
}

AudioSubmix::~AudioSubmix()
{
    unbindSubmix();
}

bool AudioSubmix::_isDirectDependencyOf(AudioSubmix* send)
{
    return m_sendGains.find(send) != m_sendGains.cend();
}

bool AudioSubmix::_mergeC3(std::list<AudioSubmix*>& output,
                           std::vector<std::list<AudioSubmix*>>& lists)
{
    for (auto outerIt = lists.begin() ; outerIt != lists.cend() ; ++outerIt)
    {
        if (outerIt->empty())
            continue;
        AudioSubmix* smx = outerIt->front();
        bool found = false;
        for (auto innerIt = lists.begin() ; innerIt != lists.cend() ; ++innerIt)
        {
            if (innerIt->empty() || outerIt == innerIt)
                continue;
            if (smx == innerIt->front())
            {
                innerIt->pop_front();
                found = true;
            }
        }
        if (found)
        {
            outerIt->pop_front();
            output.push_back(smx);
            return true;
        }
    }
    return false;
}

std::list<AudioSubmix*> AudioSubmix::_linearizeC3()
{
    std::vector<std::list<AudioSubmix*>> lists = {{}};
    for (AudioSubmix* smx : m_root.m_activeSubmixes)
    {
        if (smx == this)
            continue;
        if (smx->_isDirectDependencyOf(this))
            lists[0].push_back(smx);
    }
    lists.reserve(lists[0].size() + 1);
    for (AudioSubmix* smx : lists[0])
        lists.push_back(smx->_linearizeC3());

    std::list<AudioSubmix*> ret = {this};
    while (_mergeC3(ret, lists)) {}
    return ret;
}

void AudioSubmix::_zeroFill16()
{
    if (m_scratch16.size())
        std::fill(m_scratch16.begin(), m_scratch16.end(), 0);
}

void AudioSubmix::_zeroFill32()
{
    if (m_scratch32.size())
        std::fill(m_scratch32.begin(), m_scratch32.end(), 0);
}

void AudioSubmix::_zeroFillFlt()
{
    if (m_scratchFlt.size())
        std::fill(m_scratchFlt.begin(), m_scratchFlt.end(), 0);
}

int16_t* AudioSubmix::_getMergeBuf16(size_t frames)
{
    if (m_redirect16)
        return m_redirect16;

    size_t sampleCount = frames * m_root.m_mixInfo.m_channelMap.m_channelCount;
    if (m_scratch16.size() < sampleCount)
        m_scratch16.resize(sampleCount);

    return m_scratch16.data();
}

int32_t* AudioSubmix::_getMergeBuf32(size_t frames)
{
    if (m_redirect32)
        return m_redirect32;

    size_t sampleCount = frames * m_root.m_mixInfo.m_channelMap.m_channelCount;
    if (m_scratch32.size() < sampleCount)
        m_scratch32.resize(sampleCount);

    return m_scratch32.data();
}

float* AudioSubmix::_getMergeBufFlt(size_t frames)
{
    if (m_redirectFlt)
        return m_redirectFlt;

    size_t sampleCount = frames * m_root.m_mixInfo.m_channelMap.m_channelCount;
    if (m_scratchFlt.size() < sampleCount)
        m_scratchFlt.resize(sampleCount);

    return m_scratchFlt.data();
}

size_t AudioSubmix::_pumpAndMix16(size_t frames)
{
    ChannelMap& chMap = m_root.m_mixInfo.m_channelMap;
    size_t chanCount = chMap.m_channelCount;

    if (m_redirect16)
    {
        if (m_cb && m_cb->canApplyEffect())
            m_cb->applyEffect(m_redirect16, frames, chMap, m_root.m_mixInfo.m_sampleRate);
        m_redirect16 += chanCount * frames;
    }
    else
    {
        size_t sampleCount = frames * chanCount;
        if (m_scratch16.size() < sampleCount)
            m_scratch16.resize(sampleCount);
        if (m_cb && m_cb->canApplyEffect())
            m_cb->applyEffect(m_scratch16.data(), frames, chMap, m_root.m_mixInfo.m_sampleRate);

        size_t curSlewFrame = m_slewFrames;
        for (auto& smx : m_sendGains)
        {
            curSlewFrame = m_curSlewFrame;
            AudioSubmix& sm = *reinterpret_cast<AudioSubmix*>(smx.first);
            auto it = m_scratch16.begin();
            int16_t* dataOut = sm._getMergeBuf16(frames);

            for (size_t f=0 ; f<frames ; ++f)
            {
                if (m_slewFrames && curSlewFrame < m_slewFrames)
                {
                    double t = curSlewFrame / double(m_slewFrames);
                    double omt = 1.0 - t;

                    for (unsigned c=0 ; c<chanCount ; ++c)
                    {
                        *dataOut = Clamp16(*dataOut + *it * (smx.second[1] * t + smx.second[0] * omt));
                        ++it;
                        ++dataOut;
                    }

                    ++curSlewFrame;
                }
                else
                {
                    for (unsigned c=0 ; c<chanCount ; ++c)
                    {
                        *dataOut = Clamp16(*dataOut + *it * smx.second[1]);
                        ++it;
                        ++dataOut;
                    }
                }
            }
        }
        m_curSlewFrame += curSlewFrame;
    }

    return frames;
}

size_t AudioSubmix::_pumpAndMix32(size_t frames)
{
    ChannelMap& chMap = m_root.m_mixInfo.m_channelMap;
    size_t chanCount = chMap.m_channelCount;

    if (m_redirect32)
    {
        if (m_cb && m_cb->canApplyEffect())
            m_cb->applyEffect(m_redirect32, frames, chMap, m_root.m_mixInfo.m_sampleRate);
        m_redirect32 += chanCount * frames;
    }
    else
    {
        size_t sampleCount = frames * chanCount;
        if (m_scratch32.size() < sampleCount)
            m_scratch32.resize(sampleCount);
        if (m_cb && m_cb->canApplyEffect())
            m_cb->applyEffect(m_scratch32.data(), frames, chMap, m_root.m_mixInfo.m_sampleRate);

        size_t curSlewFrame = m_slewFrames;
        for (auto& smx : m_sendGains)
        {
            curSlewFrame = m_curSlewFrame;
            AudioSubmix& sm = *reinterpret_cast<AudioSubmix*>(smx.first);
            auto it = m_scratch32.begin();
            int32_t* dataOut = sm._getMergeBuf32(frames);

            for (size_t f=0 ; f<frames ; ++f)
            {
                if (m_slewFrames && curSlewFrame < m_slewFrames)
                {
                    double t = curSlewFrame / double(m_slewFrames);
                    double omt = 1.0 - t;

                    for (unsigned c=0 ; c<chanCount ; ++c)
                    {
                        *dataOut = Clamp32(*dataOut + *it * (smx.second[1] * t + smx.second[0] * omt));
                        ++it;
                        ++dataOut;
                    }

                    ++curSlewFrame;
                }
                else
                {
                    for (unsigned c=0 ; c<chanCount ; ++c)
                    {
                        *dataOut = Clamp32(*dataOut + *it * smx.second[1]);
                        ++it;
                        ++dataOut;
                    }
                }
            }
        }
        m_curSlewFrame += curSlewFrame;
    }

    return frames;
}

size_t AudioSubmix::_pumpAndMixFlt(size_t frames)
{
    ChannelMap& chMap = m_root.m_mixInfo.m_channelMap;
    size_t chanCount = chMap.m_channelCount;

    if (m_redirectFlt)
    {
        if (m_cb && m_cb->canApplyEffect())
            m_cb->applyEffect(m_redirectFlt, frames, chMap, m_root.m_mixInfo.m_sampleRate);
        m_redirectFlt += chanCount * frames;
    }
    else
    {
        size_t sampleCount = frames * chanCount;
        if (m_scratchFlt.size() < sampleCount)
            m_scratchFlt.resize(sampleCount);
        if (m_cb && m_cb->canApplyEffect())
            m_cb->applyEffect(m_scratchFlt.data(), frames, chMap, m_root.m_mixInfo.m_sampleRate);

        size_t curSlewFrame = m_slewFrames;
        for (auto& smx : m_sendGains)
        {
            curSlewFrame = m_curSlewFrame;
            AudioSubmix& sm = *reinterpret_cast<AudioSubmix*>(smx.first);
            auto it = m_scratchFlt.begin();
            float* dataOut = sm._getMergeBufFlt(frames);

            for (size_t f=0 ; f<frames ; ++f)
            {
                if (m_slewFrames && curSlewFrame < m_slewFrames)
                {
                    double t = curSlewFrame / double(m_slewFrames);
                    double omt = 1.0 - t;

                    for (unsigned c=0 ; c<chanCount ; ++c)
                    {
                        *dataOut = *dataOut + *it * (smx.second[1] * t + smx.second[0] * omt);
                        ++it;
                        ++dataOut;
                    }

                    ++curSlewFrame;
                }
                else
                {
                    for (unsigned c=0 ; c<chanCount ; ++c)
                    {
                        *dataOut = *dataOut + *it * smx.second[1];
                        ++it;
                        ++dataOut;
                    }
                }
            }
        }
        m_curSlewFrame += curSlewFrame;
    }

    return frames;
}

void AudioSubmix::_resetOutputSampleRate()
{
    if (m_cb)
        m_cb->resetOutputSampleRate(m_root.mixInfo().m_sampleRate);
}

void AudioSubmix::resetSendLevels()
{
    if (m_sendGains.empty())
        return;
    m_sendGains.clear();
    m_root.m_submixesDirty = true;
}

void AudioSubmix::setSendLevel(IAudioSubmix* submix, float level, bool slew)
{
    auto search = m_sendGains.find(submix);
    if (search == m_sendGains.cend())
    {
        search = m_sendGains.emplace(submix, std::array<float, 2>{1.f, 1.f}).first;
        m_root.m_submixesDirty = true;
    }

    m_slewFrames = slew ? m_root.m_5msFrames : 0;
    m_curSlewFrame = 0;

    search->second[0] = search->second[1];
    search->second[1] = level;
}

void AudioSubmix::unbindSubmix()
{
    if (m_bound)
    {
        m_root._unbindFrom(m_parentIt);
        m_bound = false;
    }
}

const AudioVoiceEngineMixInfo& AudioSubmix::mixInfo() const
{
    return m_root.mixInfo();
}

double AudioSubmix::getSampleRate() const
{
    return mixInfo().m_sampleRate;
}

SubmixFormat AudioSubmix::getSampleFormat() const
{
    switch (mixInfo().m_sampleFormat)
    {
    case SOXR_INT16_I:
    default:
        return SubmixFormat::Int16;
    case SOXR_INT32_I:
        return SubmixFormat::Int32;
    case SOXR_FLOAT32_I:
        return SubmixFormat::Float;
    }
}

}
