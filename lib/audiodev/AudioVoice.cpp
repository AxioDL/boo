#include "AudioVoice.hpp"
#include "AudioVoiceEngine.hpp"
#include "logvisor/logvisor.hpp"

namespace boo
{
static logvisor::Module Log("boo::AudioVoice");

static AudioMatrixMono DefaultMonoMtx;
static AudioMatrixStereo DefaultStereoMtx;

AudioVoice::AudioVoice(BaseAudioVoiceEngine& root,
                       IAudioVoiceCallback* cb, bool dynamicRate)
: ListNode<AudioVoice, BaseAudioVoiceEngine*, IAudioVoice>(&root), m_cb(cb), m_dynamicRate(dynamicRate)
{}

AudioVoice::~AudioVoice()
{
    soxr_delete(m_src);
}

AudioVoice*& AudioVoice::_getHeadPtr(BaseAudioVoiceEngine* head) { return head->m_voiceHead; }
std::unique_lock<std::recursive_mutex> AudioVoice::_getHeadLock(BaseAudioVoiceEngine* head)
{ return std::unique_lock<std::recursive_mutex>{head->m_dataMutex}; }
std::unique_lock<std::recursive_mutex> AudioVoice::destructorLock()
{ return std::unique_lock<std::recursive_mutex>{m_head->m_dataMutex}; }

void AudioVoice::_setPitchRatio(double ratio, bool slew)
{
    if (m_dynamicRate)
    {
        soxr_error_t err = soxr_set_io_ratio(m_src, ratio * m_sampleRateIn / m_sampleRateOut,
                                             slew ? m_head->m_5msFrames : 0);
        if (err)
        {
            Log.report(logvisor::Fatal, "unable to set resampler rate: %s", soxr_strerror(err));
            m_setPitchRatio = false;
            return;
        }
    }
    m_setPitchRatio = false;
}

void AudioVoice::_midUpdate()
{
    if (m_resetSampleRate)
        _resetSampleRate(m_deferredSampleRate);
    if (m_setPitchRatio)
        _setPitchRatio(m_pitchRatio, m_slew);
}

void AudioVoice::setPitchRatio(double ratio, bool slew)
{
    m_setPitchRatio = true;
    m_pitchRatio = ratio;
    m_slew = slew;
}

void AudioVoice::resetSampleRate(double sampleRate)
{
    m_resetSampleRate = true;
    m_deferredSampleRate = sampleRate;
}

void AudioVoice::start()
{
    m_running = true;
}

void AudioVoice::stop()
{
    m_running = false;
}

AudioVoiceMono::AudioVoiceMono(BaseAudioVoiceEngine& root, IAudioVoiceCallback* cb,
                               double sampleRate, bool dynamicRate)
: AudioVoice(root, cb, dynamicRate)
{
    _resetSampleRate(sampleRate);
}

void AudioVoiceMono::_resetSampleRate(double sampleRate)
{
    soxr_delete(m_src);

    double rateOut = m_head->mixInfo().m_sampleRate;
    soxr_datatype_t formatOut = m_head->mixInfo().m_sampleFormat;
    soxr_io_spec_t ioSpec = soxr_io_spec(SOXR_INT16_I, formatOut);
    soxr_quality_spec_t qSpec = soxr_quality_spec(SOXR_20_BITQ, m_dynamicRate ? SOXR_VR : 0);

    soxr_error_t err;
    m_src = soxr_create(sampleRate, rateOut, 1,
                        &err, &ioSpec, &qSpec, nullptr);

    if (err)
    {
        Log.report(logvisor::Fatal, "unable to create soxr resampler: %s", soxr_strerror(err));
        m_resetSampleRate = false;
        return;
    }

    m_sampleRateIn = sampleRate;
    m_sampleRateOut = rateOut;
    soxr_set_input_fn(m_src, soxr_input_fn_t(SRCCallback), this, 0);
    _setPitchRatio(m_pitchRatio, false);
    m_resetSampleRate = false;
}

size_t AudioVoiceMono::SRCCallback(AudioVoiceMono* ctx, int16_t** data, size_t frames)
{
    std::vector<int16_t>& scratchIn = ctx->m_head->m_scratchIn;
    if (scratchIn.size() < frames)
        scratchIn.resize(frames);
    *data = scratchIn.data();
    if (ctx->m_silentOut)
    {
        memset(*data, 0, frames * 2);
        return frames;
    }
    else
        return ctx->m_cb->supplyAudio(*ctx, frames, scratchIn.data());
}

bool AudioVoiceMono::isSilent() const
{
    if (m_sendMatrices.size())
    {
        for (auto& mtx : m_sendMatrices)
            if (!mtx.second.isSilent())
                return false;
        return true;
    }
    else
    {
        return DefaultMonoMtx.isSilent();
    }
}

template <typename T>
size_t AudioVoiceMono::_pumpAndMix(size_t frames)
{
    if (isSilent())
        return 0;

    auto& scratchPre = m_head->_getScratchPre<T>();
    if (scratchPre.size() < frames)
        scratchPre.resize(frames + 2);

    auto& scratchPost = m_head->_getScratchPost<T>();
    if (scratchPost.size() < frames)
        scratchPost.resize(frames + 2);

    double dt = frames / m_sampleRateOut;
    m_cb->preSupplyAudio(*this, dt);
    _midUpdate();
    size_t oDone = soxr_output(m_src, scratchPre.data(), frames);

    if (oDone)
    {
        if (m_sendMatrices.size())
        {
            for (auto& mtx : m_sendMatrices)
            {
                AudioSubmix& smx = *reinterpret_cast<AudioSubmix*>(mtx.first);
                m_cb->routeAudio(oDone, 1, dt, smx.m_busId, scratchPre.data(), scratchPost.data());
                mtx.second.mixMonoSampleData(m_head->clientMixInfo(), scratchPost.data(),
                                             smx._getMergeBuf<T>(oDone), oDone);
            }
        }
        else
        {
            AudioSubmix& smx = *m_head->m_mainSubmix;
            m_cb->routeAudio(oDone, 1, dt, m_head->m_mainSubmix->m_busId, scratchPre.data(), scratchPost.data());
            DefaultMonoMtx.mixMonoSampleData(m_head->clientMixInfo(), scratchPost.data(),
                                             smx._getMergeBuf<T>(oDone), oDone);
        }
    }

    return oDone;
}

void AudioVoiceMono::resetChannelLevels()
{
    m_head->m_submixesDirty = true;
    m_sendMatrices.clear();
}

void AudioVoiceMono::setMonoChannelLevels(IAudioSubmix* submix, const float coefs[8], bool slew)
{
    if (!submix)
        submix = m_head->m_mainSubmix.get();

    auto search = m_sendMatrices.find(submix);
    if (search == m_sendMatrices.cend())
        search = m_sendMatrices.emplace(submix, AudioMatrixMono{}).first;
    search->second.setMatrixCoefficients(coefs, slew ? m_head->m_5msFrames : 0);
}

void AudioVoiceMono::setStereoChannelLevels(IAudioSubmix* submix, const float coefs[8][2], bool slew)
{
    float newCoefs[8] =
    {
        coefs[0][0],
        coefs[1][0],
        coefs[2][0],
        coefs[3][0],
        coefs[4][0],
        coefs[5][0],
        coefs[6][0],
        coefs[7][0]
    };

    if (!submix)
        submix = m_head->m_mainSubmix.get();

    auto search = m_sendMatrices.find(submix);
    if (search == m_sendMatrices.cend())
        search = m_sendMatrices.emplace(submix, AudioMatrixMono{}).first;
    search->second.setMatrixCoefficients(newCoefs, slew ? m_head->m_5msFrames : 0);
}

AudioVoiceStereo::AudioVoiceStereo(BaseAudioVoiceEngine& root, IAudioVoiceCallback* cb,
                                   double sampleRate, bool dynamicRate)
: AudioVoice(root, cb, dynamicRate)
{
    _resetSampleRate(sampleRate);
}

void AudioVoiceStereo::_resetSampleRate(double sampleRate)
{
    soxr_delete(m_src);

    double rateOut = m_head->mixInfo().m_sampleRate;
    soxr_datatype_t formatOut = m_head->mixInfo().m_sampleFormat;
    soxr_io_spec_t ioSpec = soxr_io_spec(SOXR_INT16_I, formatOut);
    soxr_quality_spec_t qSpec = soxr_quality_spec(SOXR_20_BITQ, m_dynamicRate ? SOXR_VR : 0);

    soxr_error_t err;
    m_src = soxr_create(sampleRate, rateOut, 2,
                        &err, &ioSpec, &qSpec, nullptr);

    if (!m_src)
    {
        Log.report(logvisor::Fatal, "unable to create soxr resampler: %s", soxr_strerror(err));
        m_resetSampleRate = false;
        return;
    }

    m_sampleRateIn = sampleRate;
    m_sampleRateOut = rateOut;
    soxr_set_input_fn(m_src, soxr_input_fn_t(SRCCallback), this, 0);
    _setPitchRatio(m_pitchRatio, false);
    m_resetSampleRate = false;
}

size_t AudioVoiceStereo::SRCCallback(AudioVoiceStereo* ctx, int16_t** data, size_t frames)
{
    std::vector<int16_t>& scratchIn = ctx->m_head->m_scratchIn;
    size_t samples = frames * 2;
    if (scratchIn.size() < samples)
        scratchIn.resize(samples);
    *data = scratchIn.data();
    if (ctx->m_silentOut)
    {
        memset(*data, 0, samples * 2);
        return frames;
    }
    else
        return ctx->m_cb->supplyAudio(*ctx, frames, scratchIn.data());
}

bool AudioVoiceStereo::isSilent() const
{
    if (m_sendMatrices.size())
    {
        for (auto& mtx : m_sendMatrices)
            if (!mtx.second.isSilent())
                return false;
        return true;
    }
    else
    {
        return DefaultStereoMtx.isSilent();
    }
}

template <typename T>
size_t AudioVoiceStereo::_pumpAndMix(size_t frames)
{
    if (isSilent())
        return 0;

    size_t samples = frames * 2;

    auto& scratchPre = m_head->_getScratchPre<T>();
    if (scratchPre.size() < samples)
        scratchPre.resize(samples + 4);

    auto& scratchPost = m_head->_getScratchPost<T>();
    if (scratchPost.size() < samples)
        scratchPost.resize(samples + 4);

    double dt = frames / m_sampleRateOut;
    m_cb->preSupplyAudio(*this, dt);
    _midUpdate();
    size_t oDone = soxr_output(m_src, scratchPre.data(), frames);

    if (oDone)
    {
        if (m_sendMatrices.size())
        {
            for (auto& mtx : m_sendMatrices)
            {
                AudioSubmix& smx = *reinterpret_cast<AudioSubmix*>(mtx.first);
                m_cb->routeAudio(oDone, 2, dt, smx.m_busId, scratchPre.data(), scratchPost.data());
                mtx.second.mixStereoSampleData(m_head->clientMixInfo(), scratchPost.data(),
                                               smx._getMergeBuf<T>(oDone), oDone);
            }
        }
        else
        {
            AudioSubmix& smx = *m_head->m_mainSubmix;
            m_cb->routeAudio(oDone, 2, dt, m_head->m_mainSubmix->m_busId, scratchPre.data(), scratchPost.data());
            DefaultStereoMtx.mixStereoSampleData(m_head->clientMixInfo(), scratchPost.data(),
                                                 smx._getMergeBuf<T>(oDone), oDone);
        }
    }

    return oDone;
}

void AudioVoiceStereo::resetChannelLevels()
{
    m_head->m_submixesDirty = true;
    m_sendMatrices.clear();
}

void AudioVoiceStereo::setMonoChannelLevels(IAudioSubmix* submix, const float coefs[8], bool slew)
{
    float newCoefs[8][2] =
    {
        {coefs[0], coefs[0]},
        {coefs[1], coefs[1]},
        {coefs[2], coefs[2]},
        {coefs[3], coefs[3]},
        {coefs[4], coefs[4]},
        {coefs[5], coefs[5]},
        {coefs[6], coefs[6]},
        {coefs[7], coefs[7]}
    };

    if (!submix)
        submix = m_head->m_mainSubmix.get();

    auto search = m_sendMatrices.find(submix);
    if (search == m_sendMatrices.cend())
        search = m_sendMatrices.emplace(submix, AudioMatrixStereo{}).first;
    search->second.setMatrixCoefficients(newCoefs, slew ? m_head->m_5msFrames : 0);
}

void AudioVoiceStereo::setStereoChannelLevels(IAudioSubmix* submix, const float coefs[8][2], bool slew)
{
    if (!submix)
        submix = m_head->m_mainSubmix.get();

    auto search = m_sendMatrices.find(submix);
    if (search == m_sendMatrices.cend())
        search = m_sendMatrices.emplace(submix, AudioMatrixStereo{}).first;
    search->second.setMatrixCoefficients(coefs, slew ? m_head->m_5msFrames : 0);
}

}
