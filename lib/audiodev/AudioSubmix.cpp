#include "AudioSubmix.hpp"
#include "AudioVoiceEngine.hpp"
#include "AudioVoice.hpp"
#include <string.h>
#include <algorithm>

namespace boo
{

AudioSubmix::AudioSubmix(BaseAudioVoiceEngine& root, IAudioMix& parent, IAudioSubmixCallback* cb)
: m_root(root), m_parent(parent), m_cb(cb)
{
    std::fill(std::begin(m_gains), std::end(m_gains), 1.f);
}

AudioSubmix::~AudioSubmix()
{
    while (m_activeVoices.size())
        m_activeVoices.front()->unbindVoice();
    while (m_activeSubmixes.size())
        m_activeSubmixes.front()->unbindSubmix();
    unbindSubmix();
}

void AudioSubmix::_pumpAndMixVoices(size_t frames, int16_t* dataOut, int16_t* mainOut)
{
    const AudioVoiceEngineMixInfo& info = mixInfo();
    size_t sampleCount = frames * info.m_channelMap.m_channelCount;
    if (m_scratch16.size() < sampleCount)
        m_scratch16.resize(sampleCount);

    /* Clear target buffer */
    memset(m_scratch16.data(), 0, sizeof(int16_t) * sampleCount);

    /* Pump child voices */
    for (AudioVoice* vox : m_activeVoices)
        if (vox->m_running)
            vox->pumpAndMix(m_parent.mixInfo(), frames, mainOut, m_scratch16.data());

    /* Pump child submixes */
    for (AudioSubmix* smx : m_activeSubmixes)
        smx->_pumpAndMixVoices(frames, m_scratch16.data(), mainOut);

    /* Apply submix effect (if available) */
    if (m_cb && m_cb->canApplyEffect())
        m_cb->applyEffect(m_scratch16.data(), frames, info.m_channelMap, info.m_sampleRate);

    /* Merge into output mix */
    auto it = m_scratch16.begin();
    for (size_t f=0 ; f<frames ; ++f)
        for (size_t c=0 ; c<info.m_channelMap.m_channelCount ; ++c)
        {
            *dataOut = Clamp16(*dataOut + *it++ * m_gains[c]);
            ++dataOut;
        }
}

void AudioSubmix::_pumpAndMixVoices(size_t frames, int32_t* dataOut, int32_t* mainOut)
{
    const AudioVoiceEngineMixInfo& info = mixInfo();
    size_t sampleCount = frames * info.m_channelMap.m_channelCount;
    if (m_scratch32.size() < sampleCount)
        m_scratch32.resize(sampleCount);

    /* Clear target buffer */
    memset(m_scratch32.data(), 0, sizeof(int32_t) * sampleCount);

    /* Pump child voices */
    for (AudioVoice* vox : m_activeVoices)
        if (vox->m_running)
            vox->pumpAndMix(m_parent.mixInfo(), frames, mainOut, m_scratch32.data());

    /* Pump child submixes */
    for (AudioSubmix* smx : m_activeSubmixes)
        smx->_pumpAndMixVoices(frames, m_scratch32.data(), mainOut);

    /* Apply submix effect (if available) */
    if (m_cb && m_cb->canApplyEffect())
        m_cb->applyEffect(m_scratch32.data(), frames, info.m_channelMap, info.m_sampleRate);

    /* Merge into output mix */
    auto it = m_scratch32.begin();
    for (size_t f=0 ; f<frames ; ++f)
        for (size_t c=0 ; c<info.m_channelMap.m_channelCount ; ++c)
        {
            *dataOut = Clamp32(*dataOut + *it++ * m_gains[c]);
            ++dataOut;
        }
}

void AudioSubmix::_pumpAndMixVoices(size_t frames, float* dataOut, float* mainOut)
{
    const AudioVoiceEngineMixInfo& info = mixInfo();
    size_t sampleCount = frames * info.m_channelMap.m_channelCount;
    if (m_scratchFlt.size() < sampleCount)
        m_scratchFlt.resize(sampleCount);

    /* Clear target buffer */
    memset(m_scratchFlt.data(), 0, sizeof(float) * sampleCount);

    /* Pump child voices */
    for (AudioVoice* vox : m_activeVoices)
        if (vox->m_running)
            vox->pumpAndMix(m_parent.mixInfo(), frames, mainOut, m_scratchFlt.data());

    /* Pump child submixes */
    for (AudioSubmix* smx : m_activeSubmixes)
        smx->_pumpAndMixVoices(frames, m_scratchFlt.data(), mainOut);

    /* Apply submix effect (if available) */
    if (m_cb && m_cb->canApplyEffect())
        m_cb->applyEffect(m_scratchFlt.data(), frames, info.m_channelMap, info.m_sampleRate);

    /* Merge into output mix */
    auto it = m_scratchFlt.begin();
    for (size_t f=0 ; f<frames ; ++f)
        for (size_t c=0 ; c<info.m_channelMap.m_channelCount ; ++c)
        {
            *dataOut = *dataOut + *it++ * m_gains[c];
            ++dataOut;
        }
}

void AudioSubmix::_unbindFrom(std::list<AudioVoice*>::iterator it)
{
    m_activeVoices.erase(it);
}

void AudioSubmix::_unbindFrom(std::list<AudioSubmix*>::iterator it)
{
    m_activeSubmixes.erase(it);
}

void AudioSubmix::_resetOutputSampleRate()
{
    for (AudioVoice* vox : m_activeVoices)
        vox->_resetSampleRate(vox->m_sampleRateIn);
    for (AudioSubmix* smx : m_activeSubmixes)
        smx->_resetOutputSampleRate();
    if (m_cb)
        m_cb->resetOutputSampleRate(m_parent.mixInfo().m_sampleRate);
}

std::unique_ptr<IAudioVoice> AudioSubmix::allocateNewMonoVoice(double sampleRate,
                                                               IAudioVoiceCallback* cb,
                                                               bool dynamicPitch)
{
    std::unique_ptr<IAudioVoice> ret =
        std::make_unique<AudioVoiceMono>(m_root, *this, cb, sampleRate, dynamicPitch);
    AudioVoiceMono* retMono = static_cast<AudioVoiceMono*>(ret.get());
    retMono->bindVoice(m_activeVoices.insert(m_activeVoices.end(), retMono));
    return ret;
}

std::unique_ptr<IAudioVoice> AudioSubmix::allocateNewStereoVoice(double sampleRate,
                                                                 IAudioVoiceCallback* cb,
                                                                 bool dynamicPitch)
{
    std::unique_ptr<IAudioVoice> ret =
        std::make_unique<AudioVoiceStereo>(m_root, *this, cb, sampleRate, dynamicPitch);
    AudioVoiceStereo* retStereo = static_cast<AudioVoiceStereo*>(ret.get());
    retStereo->bindVoice(m_activeVoices.insert(m_activeVoices.end(), retStereo));
    return ret;
}

std::unique_ptr<IAudioSubmix> AudioSubmix::allocateNewSubmix(IAudioSubmixCallback* cb)
{
    std::unique_ptr<IAudioSubmix> ret =
        std::make_unique<AudioSubmix>(m_root, *this, cb);
    AudioSubmix* retIntern = static_cast<AudioSubmix*>(ret.get());
    retIntern->bindSubmix(m_activeSubmixes.insert(m_activeSubmixes.end(), retIntern));
    return ret;
}

void AudioSubmix::setChannelGains(const float gains[8])
{
    for (int i=0 ; i<8 ; ++i)
        m_gains[i] = gains[i];
}

void AudioSubmix::unbindSubmix()
{
    if (m_bound)
    {
        m_parent._unbindFrom(m_parentIt);
        m_bound = false;
    }
}

const AudioVoiceEngineMixInfo& AudioSubmix::mixInfo() const
{
    return m_parent.mixInfo();
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
