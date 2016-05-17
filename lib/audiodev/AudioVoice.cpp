#include "AudioVoice.hpp"
#include "AudioVoiceEngine.hpp"
#include "logvisor/logvisor.hpp"

namespace boo
{
static logvisor::Module Log("boo::AudioVoice");

AudioVoice::AudioVoice(BaseAudioVoiceEngine& root, IAudioMix& parent,
                       IAudioVoiceCallback* cb, bool dynamicRate)
: m_root(root), m_parent(parent), m_cb(cb), m_dynamicRate(dynamicRate) {}

AudioVoice::~AudioVoice()
{
    unbindVoice();
    soxr_delete(m_src);
}

void AudioVoice::_setPitchRatio(double ratio, bool slew)
{
    if (m_dynamicRate)
    {
        soxr_error_t err = soxr_set_io_ratio(m_src, ratio * m_sampleRateIn / m_sampleRateOut, slew ? m_5msFrames : 0);
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

void AudioVoice::unbindVoice()
{
    if (m_bound)
    {
        m_parent._unbindFrom(m_parentIt);
        m_bound = false;
    }
}

AudioVoiceMono::AudioVoiceMono(BaseAudioVoiceEngine& root, IAudioMix& parent, IAudioVoiceCallback* cb,
                               double sampleRate, bool dynamicRate)
: AudioVoice(root, parent, cb, dynamicRate)
{
    _resetSampleRate(sampleRate);
}

void AudioVoiceMono::_resetSampleRate(double sampleRate)
{
    soxr_delete(m_src);

    double rateOut = m_parent.mixInfo().m_sampleRate;
    soxr_io_spec_t ioSpec = soxr_io_spec(SOXR_INT16_I, m_parent.mixInfo().m_sampleFormat);
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
    m_5msFrames = rateOut * 5 / 1000;
    soxr_set_input_fn(m_src, soxr_input_fn_t(SRCCallback), this, 0);
    _setPitchRatio(m_pitchRatio, false);
    m_resetSampleRate = false;
}

size_t AudioVoiceMono::SRCCallback(AudioVoiceMono* ctx, int16_t** data, size_t frames)
{
    std::vector<int16_t>& scratchIn = ctx->m_root.m_scratchIn;
    if (scratchIn.size() < frames)
        scratchIn.resize(frames);
    *data = scratchIn.data();
    return ctx->m_cb->supplyAudio(*ctx, frames, scratchIn.data());
}

size_t AudioVoiceMono::pumpAndMix(const AudioVoiceEngineMixInfo& mixInfo,
                                  size_t frames, int16_t* buf)
{
    std::vector<int16_t>& scratch16 = m_root.m_scratch16;
    if (scratch16.size() < frames)
        scratch16.resize(frames);

    size_t oDone;
    size_t totalDone = 0;
    while (frames)
    {
        oDone = soxr_output(m_src, scratch16.data(), m_5msFrames);
        _midUpdate();
        if (oDone)
        {
            buf = m_matrix.mixMonoSampleData(mixInfo, scratch16.data(), buf, oDone);
            totalDone += oDone;
            frames -= oDone;
        }
    }

    return totalDone;
}

size_t AudioVoiceMono::pumpAndMix(const AudioVoiceEngineMixInfo& mixInfo,
                                  size_t frames, int32_t* buf)
{
    std::vector<int32_t>& scratch32 = m_root.m_scratch32;
    if (scratch32.size() < frames)
        scratch32.resize(frames);

    size_t oDone;
    size_t totalDone = 0;
    while (frames)
    {
        oDone = soxr_output(m_src, scratch32.data(), m_5msFrames);
        _midUpdate();
        if (oDone)
        {
            buf = m_matrix.mixMonoSampleData(mixInfo, scratch32.data(), buf, oDone);
            totalDone += oDone;
            frames -= oDone;
        }
    }

    return totalDone;
}

size_t AudioVoiceMono::pumpAndMix(const AudioVoiceEngineMixInfo& mixInfo,
                                  size_t frames, float* buf)
{
    std::vector<float>& scratchFlt = m_root.m_scratchFlt;
    if (scratchFlt.size() < frames)
        scratchFlt.resize(frames);

    size_t oDone;
    size_t totalDone = 0;
    while (frames)
    {
        oDone = soxr_output(m_src, scratchFlt.data(), m_5msFrames);
        _midUpdate();
        if (oDone)
        {
            buf = m_matrix.mixMonoSampleData(mixInfo, scratchFlt.data(), buf, oDone);
            totalDone += oDone;
            frames -= oDone;
        }
    }

    return totalDone;
}

void AudioVoiceMono::setDefaultMatrixCoefficients()
{
    m_matrix.setDefaultMatrixCoefficients(m_parent.mixInfo().m_channels);
}

void AudioVoiceMono::setMonoMatrixCoefficients(const float coefs[8])
{
    m_matrix.setMatrixCoefficients(coefs);
}

void AudioVoiceMono::setStereoMatrixCoefficients(const float coefs[8][2])
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
    m_matrix.setMatrixCoefficients(newCoefs);
}

AudioVoiceStereo::AudioVoiceStereo(BaseAudioVoiceEngine& root, IAudioMix& parent, IAudioVoiceCallback* cb,
                                   double sampleRate, bool dynamicRate)
: AudioVoice(root, parent, cb, dynamicRate)
{
    _resetSampleRate(sampleRate);
}

void AudioVoiceStereo::_resetSampleRate(double sampleRate)
{
    soxr_delete(m_src);

    double rateOut = m_parent.mixInfo().m_sampleRate;
    soxr_io_spec_t ioSpec = soxr_io_spec(SOXR_INT16_I, m_parent.mixInfo().m_sampleFormat);
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
    std::vector<int16_t>& scratchIn = ctx->m_root.m_scratchIn;
    size_t samples = frames * 2;
    if (scratchIn.size() < samples)
        scratchIn.resize(samples);
    *data = scratchIn.data();
    return ctx->m_cb->supplyAudio(*ctx, frames, scratchIn.data());
}

size_t AudioVoiceStereo::pumpAndMix(const AudioVoiceEngineMixInfo& mixInfo,
                                    size_t frames, int16_t* buf)
{
    std::vector<int16_t>& scratch16 = m_root.m_scratch16;
    size_t samples = frames * 2;
    if (scratch16.size() < samples)
        scratch16.resize(samples);

    size_t oDone;
    size_t totalDone = 0;
    while (frames)
    {
        oDone = soxr_output(m_src, scratch16.data(), m_5msFrames);
        _midUpdate();
        if (oDone)
        {
            buf = m_matrix.mixStereoSampleData(mixInfo, scratch16.data(), buf, oDone);
            totalDone += oDone;
            frames -= oDone;
        }
    }

    return totalDone;
}

size_t AudioVoiceStereo::pumpAndMix(const AudioVoiceEngineMixInfo& mixInfo,
                                    size_t frames, int32_t* buf)
{
    std::vector<int32_t>& scratch32 = m_root.m_scratch32;
    size_t samples = frames * 2;
    if (scratch32.size() < samples)
        scratch32.resize(samples);

    size_t oDone;
    size_t totalDone = 0;
    while (frames)
    {
        oDone = soxr_output(m_src, scratch32.data(), m_5msFrames);
        _midUpdate();
        if (oDone)
        {
            buf = m_matrix.mixStereoSampleData(mixInfo, scratch32.data(), buf, oDone);
            totalDone += oDone;
            frames -= oDone;
        }
    }

    return totalDone;
}

size_t AudioVoiceStereo::pumpAndMix(const AudioVoiceEngineMixInfo& mixInfo,
                                    size_t frames, float* buf)
{
    std::vector<float>& scratchFlt = m_root.m_scratchFlt;
    size_t samples = frames * 2;
    if (scratchFlt.size() < samples)
        scratchFlt.resize(samples);

    size_t oDone;
    size_t totalDone = 0;
    while (frames)
    {
        oDone = soxr_output(m_src, scratchFlt.data(), m_5msFrames);
        _midUpdate();
        if (oDone)
        {
            buf = m_matrix.mixStereoSampleData(mixInfo, scratchFlt.data(), buf, oDone);
            totalDone += oDone;
            frames -= oDone;
        }
    }

    return totalDone;
}

void AudioVoiceStereo::setDefaultMatrixCoefficients()
{
    m_matrix.setDefaultMatrixCoefficients(m_parent.mixInfo().m_channels);
}

void AudioVoiceStereo::setMonoMatrixCoefficients(const float coefs[8])
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
    m_matrix.setMatrixCoefficients(newCoefs);
}

void AudioVoiceStereo::setStereoMatrixCoefficients(const float coefs[8][2])
{
    m_matrix.setMatrixCoefficients(coefs);
}

}
