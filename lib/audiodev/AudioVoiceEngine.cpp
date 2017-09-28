#include "AudioVoiceEngine.hpp"
#include "LtRtProcessing.hpp"

namespace boo
{

BaseAudioVoiceEngine::~BaseAudioVoiceEngine()
{
    while (m_activeVoices.size())
        m_activeVoices.front()->unbindVoice();
    while (m_activeSubmixes.size())
        m_activeSubmixes.front()->unbindSubmix();
}

void BaseAudioVoiceEngine::_pumpAndMixVoices(size_t frames, int16_t* dataOut)
{
    memset(dataOut, 0, sizeof(int16_t) * frames * m_mixInfo.m_channelMap.m_channelCount);
    if (m_ltRtProcessing)
    {
        size_t sampleCount = m_5msFrames * 5;
        if (m_ltRtIn16.size() < sampleCount)
            m_ltRtIn16.resize(sampleCount);
        m_mainSubmix.m_redirect16 = m_ltRtIn16.data();
    }
    else
    {
        m_mainSubmix.m_redirect16 = dataOut;
    }

    if (m_submixesDirty)
    {
        m_linearizedSubmixes = m_mainSubmix._linearizeC3();
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

        for (auto it = m_linearizedSubmixes.rbegin() ; it != m_linearizedSubmixes.rend() ; ++it)
            (*it)->_zeroFill16();

        for (AudioVoice* vox : m_activeVoices)
            if (vox->m_running)
                vox->pumpAndMix16(thisFrames);

        for (auto it = m_linearizedSubmixes.rbegin() ; it != m_linearizedSubmixes.rend() ; ++it)
            (*it)->_pumpAndMix16(thisFrames);

        if (m_ltRtProcessing)
        {
            m_ltRtProcessing->Process(m_ltRtIn16.data(), dataOut, int(thisFrames));
            m_mainSubmix.m_redirect16 = m_ltRtIn16.data();
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

void BaseAudioVoiceEngine::_pumpAndMixVoices(size_t frames, int32_t* dataOut)
{
    memset(dataOut, 0, sizeof(int32_t) * frames * m_mixInfo.m_channelMap.m_channelCount);
    if (m_ltRtProcessing)
    {
        size_t sampleCount = m_5msFrames * 5;
        if (m_ltRtIn32.size() < sampleCount)
            m_ltRtIn32.resize(sampleCount);
        m_mainSubmix.m_redirect32 = m_ltRtIn32.data();
    }
    else
    {
        m_mainSubmix.m_redirect32 = dataOut;
    }

    if (m_submixesDirty)
    {
        m_linearizedSubmixes = m_mainSubmix._linearizeC3();
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

        for (auto it = m_linearizedSubmixes.rbegin() ; it != m_linearizedSubmixes.rend() ; ++it)
            (*it)->_zeroFill32();

        for (AudioVoice* vox : m_activeVoices)
            if (vox->m_running)
                vox->pumpAndMix32(thisFrames);

        for (auto it = m_linearizedSubmixes.rbegin() ; it != m_linearizedSubmixes.rend() ; ++it)
            (*it)->_pumpAndMix32(thisFrames);

        if (m_ltRtProcessing)
        {
            m_ltRtProcessing->Process(m_ltRtIn32.data(), dataOut, int(thisFrames));
            m_mainSubmix.m_redirect32 = m_ltRtIn32.data();
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

void BaseAudioVoiceEngine::_pumpAndMixVoices(size_t frames, float* dataOut)
{
    memset(dataOut, 0, sizeof(float) * frames * m_mixInfo.m_channelMap.m_channelCount);
    if (m_ltRtProcessing)
    {
        size_t sampleCount = m_5msFrames * 5;
        if (m_ltRtInFlt.size() < sampleCount)
            m_ltRtInFlt.resize(sampleCount);
        m_mainSubmix.m_redirectFlt = m_ltRtInFlt.data();
    }
    else
    {
        m_mainSubmix.m_redirectFlt = dataOut;
    }

    if (m_submixesDirty)
    {
        m_linearizedSubmixes = m_mainSubmix._linearizeC3();
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

        for (auto it = m_linearizedSubmixes.rbegin() ; it != m_linearizedSubmixes.rend() ; ++it)
            (*it)->_zeroFillFlt();

        for (AudioVoice* vox : m_activeVoices)
            if (vox->m_running)
                vox->pumpAndMixFlt(thisFrames);

        for (auto it = m_linearizedSubmixes.rbegin() ; it != m_linearizedSubmixes.rend() ; ++it)
            (*it)->_pumpAndMixFlt(thisFrames);

        if (m_ltRtProcessing)
        {
            m_ltRtProcessing->Process(m_ltRtInFlt.data(), dataOut, int(thisFrames));
            m_mainSubmix.m_redirectFlt = m_ltRtInFlt.data();
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

void BaseAudioVoiceEngine::_unbindFrom(std::list<AudioVoice*>::iterator it)
{
    m_activeVoices.erase(it);
}

void BaseAudioVoiceEngine::_unbindFrom(std::list<AudioSubmix*>::iterator it)
{
    m_activeSubmixes.erase(it);
    m_submixesDirty = true;
}

std::unique_ptr<IAudioVoice>
BaseAudioVoiceEngine::allocateNewMonoVoice(double sampleRate,
                                           IAudioVoiceCallback* cb,
                                           bool dynamicPitch)
{
    std::unique_ptr<IAudioVoice> ret =
        std::make_unique<AudioVoiceMono>(*this, cb, sampleRate, dynamicPitch);
    AudioVoiceMono* retMono = static_cast<AudioVoiceMono*>(ret.get());
    retMono->bindVoice(m_activeVoices.insert(m_activeVoices.end(), retMono));
    return ret;
}

std::unique_ptr<IAudioVoice>
BaseAudioVoiceEngine::allocateNewStereoVoice(double sampleRate,
                                             IAudioVoiceCallback* cb,
                                             bool dynamicPitch)
{
    std::unique_ptr<IAudioVoice> ret =
        std::make_unique<AudioVoiceStereo>(*this, cb, sampleRate, dynamicPitch);
    AudioVoiceStereo* retStereo = static_cast<AudioVoiceStereo*>(ret.get());
    retStereo->bindVoice(m_activeVoices.insert(m_activeVoices.end(), retStereo));
    return ret;
}

std::unique_ptr<IAudioSubmix>
BaseAudioVoiceEngine::allocateNewSubmix(bool mainOut, IAudioSubmixCallback* cb, int busId)
{
    std::unique_ptr<IAudioSubmix> ret = std::make_unique<AudioSubmix>(*this, cb, busId, mainOut);
    AudioSubmix* retIntern = static_cast<AudioSubmix*>(ret.get());
    retIntern->bindSubmix(m_activeSubmixes.insert(m_activeSubmixes.end(), retIntern));
    return ret;
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
