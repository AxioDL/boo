#include "AudioVoiceEngine.hpp"
#include <cassert>

namespace boo
{

BaseAudioVoiceEngine::~BaseAudioVoiceEngine()
{
    m_mainSubmix.reset();
    assert(m_voiceHead == nullptr && "Dangling voices detected");
    assert(m_submixHead == nullptr && "Dangling submixes detected");
}

template <typename T>
void BaseAudioVoiceEngine::_pumpAndMixVoices(size_t frames, T* dataOut)
{
    memset(dataOut, 0, sizeof(T) * frames * m_mixInfo.m_channelMap.m_channelCount);
    if (m_ltRtProcessing)
    {
        size_t sampleCount = m_5msFrames * 5;
        if (_getLtRtIn<T>().size() < sampleCount)
            _getLtRtIn<T>().resize(sampleCount);
        m_mainSubmix->_getRedirect<T>() = _getLtRtIn<T>().data();
    }
    else
    {
        m_mainSubmix->_getRedirect<T>() = dataOut;
    }

    if (m_submixesDirty)
    {
        m_linearizedSubmixes = m_mainSubmix->_linearizeC3();
        m_submixesDirty = false;
    }

    size_t remFrames = frames;
    while (remFrames)
    {
        size_t thisFrames;
        if (remFrames < m_5msFrames)
        {
            thisFrames = remFrames;
            if (m_engineCallback)
                m_engineCallback->on5MsInterval(*this, thisFrames / double(m_5msFrames) * 5.0 / 1000.0);
        }
        else
        {
            thisFrames = m_5msFrames;
            if (m_engineCallback)
                m_engineCallback->on5MsInterval(*this, 5.0 / 1000.0);
        }

        if (m_ltRtProcessing)
            std::fill(_getLtRtIn<T>().begin(), _getLtRtIn<T>().end(), 0.f);

        for (auto it = m_linearizedSubmixes.rbegin() ; it != m_linearizedSubmixes.rend() ; ++it)
            (*it)->_zeroFill<T>();

        if (m_voiceHead)
            for (AudioVoice& vox : *m_voiceHead)
                if (vox.m_running)
                    vox.pumpAndMix<T>(thisFrames);

        for (auto it = m_linearizedSubmixes.rbegin() ; it != m_linearizedSubmixes.rend() ; ++it)
            (*it)->_pumpAndMix<T>(thisFrames);

        if (m_ltRtProcessing)
        {
            m_ltRtProcessing->Process(_getLtRtIn<T>().data(), dataOut, int(thisFrames));
            m_mainSubmix->_getRedirect<T>() = _getLtRtIn<T>().data();
        }

        size_t sampleCount = thisFrames * m_mixInfo.m_channelMap.m_channelCount;
        for (size_t i=0 ; i<sampleCount ; ++i)
            dataOut[i] *= m_totalVol;

        remFrames -= thisFrames;
        dataOut += sampleCount;
    }

    if (m_engineCallback)
        m_engineCallback->onPumpCycleComplete(*this);
}

template void BaseAudioVoiceEngine::_pumpAndMixVoices<int16_t>(size_t frames, int16_t* dataOut);
template void BaseAudioVoiceEngine::_pumpAndMixVoices<int32_t>(size_t frames, int32_t* dataOut);
template void BaseAudioVoiceEngine::_pumpAndMixVoices<float>(size_t frames, float* dataOut);

std::unique_ptr<IAudioVoice>
BaseAudioVoiceEngine::allocateNewMonoVoice(double sampleRate,
                                           IAudioVoiceCallback* cb,
                                           bool dynamicPitch)
{
    return std::make_unique<AudioVoiceMono>(*this, cb, sampleRate, dynamicPitch);
}

std::unique_ptr<IAudioVoice>
BaseAudioVoiceEngine::allocateNewStereoVoice(double sampleRate,
                                             IAudioVoiceCallback* cb,
                                             bool dynamicPitch)
{
    return std::make_unique<AudioVoiceStereo>(*this, cb, sampleRate, dynamicPitch);
}

std::unique_ptr<IAudioSubmix>
BaseAudioVoiceEngine::allocateNewSubmix(bool mainOut, IAudioSubmixCallback* cb, int busId)
{
    return std::make_unique<AudioSubmix>(*this, cb, busId, mainOut);
}

void BaseAudioVoiceEngine::setCallbackInterface(IAudioVoiceEngineCallback* cb)
{
    m_engineCallback = cb;
}

void BaseAudioVoiceEngine::setVolume(float vol)
{
    m_totalVol = vol;
}

bool BaseAudioVoiceEngine::enableLtRt(bool enable)
{
    if (enable && m_mixInfo.m_channelMap.m_channelCount == 2 &&
        m_mixInfo.m_channels == AudioChannelSet::Stereo)
        m_ltRtProcessing = std::make_unique<LtRtProcessing>(m_5msFrames, m_mixInfo);
    else
        m_ltRtProcessing.reset();
    return m_ltRtProcessing.operator bool();
}

const AudioVoiceEngineMixInfo& BaseAudioVoiceEngine::mixInfo() const
{
    return m_mixInfo;
}

const AudioVoiceEngineMixInfo& BaseAudioVoiceEngine::clientMixInfo() const
{
    return m_ltRtProcessing ? m_ltRtProcessing->inMixInfo() : m_mixInfo;
}

}
