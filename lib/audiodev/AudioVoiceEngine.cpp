#include "AudioVoiceEngine.hpp"
#include <string.h>

namespace boo
{

void BaseAudioVoiceEngine::_pumpAndMixVoices(size_t frames, int16_t* dataOut)
{
    memset(dataOut, 0, sizeof(int16_t) * frames * m_mixInfo.m_channelMap.m_channelCount);
    for (AudioVoice* vox : m_activeVoices)
        if (vox->m_running)
            vox->pumpAndMix(m_mixInfo, frames, dataOut);
}

void BaseAudioVoiceEngine::_pumpAndMixVoices(size_t frames, int32_t* dataOut)
{
    memset(dataOut, 0, sizeof(int32_t) * frames * m_mixInfo.m_channelMap.m_channelCount);
    for (AudioVoice* vox : m_activeVoices)
        if (vox->m_running)
            vox->pumpAndMix(m_mixInfo, frames, dataOut);
}

void BaseAudioVoiceEngine::_pumpAndMixVoices(size_t frames, float* dataOut)
{
    memset(dataOut, 0, sizeof(float) * frames * m_mixInfo.m_channelMap.m_channelCount);
    for (AudioVoice* vox : m_activeVoices)
        if (vox->m_running)
            vox->pumpAndMix(m_mixInfo, frames, dataOut);
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

}
