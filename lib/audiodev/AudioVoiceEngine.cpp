#include "AudioVoiceEngine.hpp"
#include <string.h>

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

    size_t remFrames = frames;
    while (remFrames)
    {
        size_t thisFrames;
        if (remFrames < m_5msFrames)
        {
            thisFrames = remFrames;
            if (m_5msCallback)
                m_5msCallback(thisFrames / double(m_5msFrames) * 5.0 / 1000.0);
        }
        else
        {
            thisFrames = m_5msFrames;
            if (m_5msCallback)
                m_5msCallback(5.0 / 1000.0);
        }

        for (AudioVoice* vox : m_activeVoices)
            if (vox->m_running)
                vox->pumpAndMix(m_mixInfo, thisFrames, dataOut);
        for (AudioSubmix* smx : m_activeSubmixes)
            smx->_pumpAndMixVoices(thisFrames, dataOut);
        remFrames -= thisFrames;
        dataOut += thisFrames * m_mixInfo.m_channelMap.m_channelCount;
    }
}

void BaseAudioVoiceEngine::_pumpAndMixVoices(size_t frames, int32_t* dataOut)
{
    memset(dataOut, 0, sizeof(int32_t) * frames * m_mixInfo.m_channelMap.m_channelCount);

    size_t remFrames = frames;
    while (remFrames)
    {
        size_t thisFrames;
        if (remFrames < m_5msFrames)
        {
            thisFrames = remFrames;
            if (m_5msCallback)
                m_5msCallback(thisFrames / double(m_5msFrames) * 5.0 / 1000.0);
        }
        else
        {
            thisFrames = m_5msFrames;
            if (m_5msCallback)
                m_5msCallback(5.0 / 1000.0);
        }

        for (AudioVoice* vox : m_activeVoices)
            if (vox->m_running)
                vox->pumpAndMix(m_mixInfo, thisFrames, dataOut);
        for (AudioSubmix* smx : m_activeSubmixes)
            smx->_pumpAndMixVoices(thisFrames, dataOut);
        remFrames -= thisFrames;
        dataOut += thisFrames * m_mixInfo.m_channelMap.m_channelCount;
    }
}

void BaseAudioVoiceEngine::_pumpAndMixVoices(size_t frames, float* dataOut)
{
    memset(dataOut, 0, sizeof(float) * frames * m_mixInfo.m_channelMap.m_channelCount);

    size_t remFrames = frames;
    while (remFrames)
    {
        size_t thisFrames;
        if (remFrames < m_5msFrames)
        {
            thisFrames = remFrames;
            if (m_5msCallback)
                m_5msCallback(thisFrames / double(m_5msFrames) * 5.0 / 1000.0);
        }
        else
        {
            thisFrames = m_5msFrames;
            if (m_5msCallback)
                m_5msCallback(5.0 / 1000.0);
        }

        for (AudioVoice* vox : m_activeVoices)
            if (vox->m_running)
                vox->pumpAndMix(m_mixInfo, thisFrames, dataOut);
        for (AudioSubmix* smx : m_activeSubmixes)
            smx->_pumpAndMixVoices(thisFrames, dataOut);
        remFrames -= thisFrames;
        dataOut += thisFrames * m_mixInfo.m_channelMap.m_channelCount;
    }
}

void BaseAudioVoiceEngine::_unbindFrom(std::list<AudioVoice*>::iterator it)
{
    m_activeVoices.erase(it);
}

void BaseAudioVoiceEngine::_unbindFrom(std::list<AudioSubmix*>::iterator it)
{
    m_activeSubmixes.erase(it);
}

std::unique_ptr<IAudioVoice>
BaseAudioVoiceEngine::allocateNewMonoVoice(double sampleRate,
                                           IAudioVoiceCallback* cb,
                                           bool dynamicPitch)
{
    std::unique_ptr<IAudioVoice> ret =
        std::make_unique<AudioVoiceMono>(*this, *this, cb, sampleRate, dynamicPitch);
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
        std::make_unique<AudioVoiceStereo>(*this, *this, cb, sampleRate, dynamicPitch);
    AudioVoiceStereo* retStereo = static_cast<AudioVoiceStereo*>(ret.get());
    retStereo->bindVoice(m_activeVoices.insert(m_activeVoices.end(), retStereo));
    return ret;
}

std::unique_ptr<IAudioSubmix>
BaseAudioVoiceEngine::allocateNewSubmix(IAudioSubmixCallback* cb)
{
    std::unique_ptr<IAudioSubmix> ret =
        std::make_unique<AudioSubmix>(*this, *this, cb);
    AudioSubmix* retIntern = static_cast<AudioSubmix*>(ret.get());
    retIntern->bindSubmix(m_activeSubmixes.insert(m_activeSubmixes.end(), retIntern));
    return ret;
}

void BaseAudioVoiceEngine::register5MsCallback(std::function<void(double dt)>&& callback)
{
    m_5msCallback = std::move(callback);
}

const AudioVoiceEngineMixInfo& BaseAudioVoiceEngine::mixInfo() const
{
    return m_mixInfo;
}

}
