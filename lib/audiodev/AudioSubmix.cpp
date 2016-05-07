#include "AudioSubmix.hpp"
#include "AudioVoiceEngine.hpp"
#include "AudioVoice.hpp"
#include <string.h>

namespace boo
{

static std::vector<int16_t> scratch16;
static std::vector<int32_t> scratch32;
static std::vector<float> scratchFlt;

AudioSubmix::AudioSubmix(IAudioHost& parent, IAudioSubmixCallback* cb)
: m_parent(parent), m_cb(cb) {}

AudioSubmix::~AudioSubmix()
{
    unbindSubmix();
}

void AudioSubmix::_pumpAndMixVoices(size_t frames, int16_t* dataOut)
{
    const AudioVoiceEngineMixInfo& info = mixInfo();
    size_t sampleCount = frames * info.m_channelMap.m_channelCount;
    if (scratch16.size() < sampleCount)
        scratch16.resize(sampleCount);

    /* Clear target buffer */
    memset(scratch16.data(), 0, sizeof(int16_t) * sampleCount);

    /* Pump child voices */
    for (AudioVoice* vox : m_activeVoices)
        if (vox->m_running)
            vox->pumpAndMix(m_parent.mixInfo(), frames, scratch16.data());

    /* Pump child submixes */
    for (AudioSubmix* smx : m_activeSubmixes)
        smx->_pumpAndMixVoices(frames, scratch16.data());

    /* Apply submix effect (if available) */
    if (m_cb && m_cb->canApplyEffect())
        m_cb->applyEffect(scratch16.data(), info.m_channelMap, info.m_sampleRate);

    /* Merge into output mix */
    auto it = scratch16.begin();
    for (size_t f=0 ; f<frames ; ++f)
        for (size_t c=0 ; c<info.m_channelMap.m_channelCount ; ++c)
            *dataOut++ = Clamp16(*it++ * m_gains[c]);
}

void AudioSubmix::_pumpAndMixVoices(size_t frames, int32_t* dataOut)
{
    const AudioVoiceEngineMixInfo& info = mixInfo();
    size_t sampleCount = frames * info.m_channelMap.m_channelCount;
    if (scratch32.size() < sampleCount)
        scratch32.resize(sampleCount);

    /* Clear target buffer */
    memset(scratch32.data(), 0, sizeof(int32_t) * sampleCount);

    /* Pump child voices */
    for (AudioVoice* vox : m_activeVoices)
        if (vox->m_running)
            vox->pumpAndMix(m_parent.mixInfo(), frames, scratch32.data());

    /* Pump child submixes */
    for (AudioSubmix* smx : m_activeSubmixes)
        smx->_pumpAndMixVoices(frames, scratch32.data());

    /* Apply submix effect (if available) */
    if (m_cb && m_cb->canApplyEffect())
        m_cb->applyEffect(scratch32.data(), info.m_channelMap, info.m_sampleRate);

    /* Merge into output mix */
    auto it = scratch32.begin();
    for (size_t f=0 ; f<frames ; ++f)
        for (size_t c=0 ; c<info.m_channelMap.m_channelCount ; ++c)
            *dataOut++ = Clamp32(*it++ * m_gains[c]);
}

void AudioSubmix::_pumpAndMixVoices(size_t frames, float* dataOut)
{
    const AudioVoiceEngineMixInfo& info = mixInfo();
    size_t sampleCount = frames * info.m_channelMap.m_channelCount;
    if (scratchFlt.size() < sampleCount)
        scratchFlt.resize(sampleCount);

    /* Clear target buffer */
    memset(scratchFlt.data(), 0, sizeof(float) * sampleCount);

    /* Pump child voices */
    for (AudioVoice* vox : m_activeVoices)
        if (vox->m_running)
            vox->pumpAndMix(m_parent.mixInfo(), frames, scratchFlt.data());

    /* Pump child submixes */
    for (AudioSubmix* smx : m_activeSubmixes)
        smx->_pumpAndMixVoices(frames, scratchFlt.data());

    /* Apply submix effect (if available) */
    if (m_cb && m_cb->canApplyEffect())
        m_cb->applyEffect(scratchFlt.data(), info.m_channelMap, info.m_sampleRate);

    /* Merge into output mix */
    auto it = scratchFlt.begin();
    for (size_t f=0 ; f<frames ; ++f)
        for (size_t c=0 ; c<info.m_channelMap.m_channelCount ; ++c)
            *dataOut++ = ClampFlt(*it++ * m_gains[c]);
}

void AudioSubmix::_unbindFrom(std::list<AudioVoice*>::iterator it)
{
    m_activeVoices.erase(it);
}

void AudioSubmix::_unbindFrom(std::list<AudioSubmix*>::iterator it)
{
    m_activeSubmixes.erase(it);
}

std::unique_ptr<IAudioVoice> AudioSubmix::allocateNewMonoVoice(double sampleRate,
                                                               IAudioVoiceCallback* cb,
                                                               bool dynamicPitch)
{
    std::unique_ptr<IAudioVoice> ret =
        std::make_unique<AudioVoiceMono>(*this, cb, sampleRate, dynamicPitch);
    AudioVoiceMono* retMono = static_cast<AudioVoiceMono*>(ret.get());
    retMono->bindVoice(m_activeVoices.insert(m_activeVoices.end(), retMono));
    return ret;
}

std::unique_ptr<IAudioVoice> AudioSubmix::allocateNewStereoVoice(double sampleRate,
                                                                 IAudioVoiceCallback* cb,
                                                                 bool dynamicPitch)
{
    std::unique_ptr<IAudioVoice> ret =
        std::make_unique<AudioVoiceStereo>(*this, cb, sampleRate, dynamicPitch);
    AudioVoiceStereo* retStereo = static_cast<AudioVoiceStereo*>(ret.get());
    retStereo->bindVoice(m_activeVoices.insert(m_activeVoices.end(), retStereo));
    return ret;
}

std::unique_ptr<IAudioSubmix> AudioSubmix::allocateNewSubmix(IAudioSubmixCallback* cb)
{
    std::unique_ptr<IAudioSubmix> ret =
        std::make_unique<AudioSubmix>(*this, cb);
    AudioSubmix* retIntern = static_cast<AudioSubmix*>(ret.get());
    retIntern->bindSubmix(m_activeSubmixes.insert(m_activeSubmixes.end(), retIntern));
    return ret;
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

}
