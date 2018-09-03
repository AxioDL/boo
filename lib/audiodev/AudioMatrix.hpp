#ifndef BOO_AUDIOMATRIX_HPP
#define BOO_AUDIOMATRIX_HPP

#include "boo/audiodev/IAudioVoice.hpp"
#include <vector>
#include <cstdint>
#include <limits.h>
#include <cfloat>

#if __SSE__
#include <immintrin.h>
#endif

namespace boo
{
struct AudioVoiceEngineMixInfo;

static inline int16_t Clamp16(float in)
{
    if (in < SHRT_MIN)
        return SHRT_MIN;
    else if (in > SHRT_MAX)
        return SHRT_MAX;
    return in;
}

static inline int32_t Clamp32(float in)
{
    if (in < INT_MIN)
        return INT_MIN;
    else if (in > INT_MAX)
        return INT_MAX;
    return in;
}

class AudioMatrixMono
{
    union Coefs
    {
        float v[8];
#if __SSE__
        __m128 q[2];
        __m64 d[4];
#endif
    };
    Coefs m_coefs = {};
    Coefs m_oldCoefs = {};
    size_t m_slewFrames = 0;
    size_t m_curSlewFrame = ~size_t(0);
public:
    AudioMatrixMono() {setDefaultMatrixCoefficients(AudioChannelSet::Stereo);}

    void setDefaultMatrixCoefficients(AudioChannelSet acSet);
    void setMatrixCoefficients(const float coefs[8], size_t slewFrames=0)
    {
        m_slewFrames = slewFrames;
#if __SSE__
        if (m_curSlewFrame != 0)
        {
            m_oldCoefs.q[0] = m_coefs.q[0];
            m_oldCoefs.q[1] = m_coefs.q[1];
        }
        m_coefs.q[0] = _mm_loadu_ps(coefs);
        m_coefs.q[1] = _mm_loadu_ps(&coefs[4]);
#else
        for (int i=0 ; i<8 ; ++i)
        {
            if (m_curSlewFrame != 0)
                m_oldCoefs.v[i] = m_coefs.v[i];
            m_coefs.v[i] = coefs[i];
        }
#endif
        m_curSlewFrame = 0;
    }

    int16_t* mixMonoSampleData(const AudioVoiceEngineMixInfo& info,
                               const int16_t* dataIn, int16_t* dataOut, size_t samples);
    int32_t* mixMonoSampleData(const AudioVoiceEngineMixInfo& info,
                               const int32_t* dataIn, int32_t* dataOut, size_t samples);
    float* mixMonoSampleData(const AudioVoiceEngineMixInfo& info,
                             const float* dataIn, float* dataOut, size_t samples);

    bool isSilent() const
    {
        if (m_curSlewFrame < m_slewFrames)
            for (int i=0 ; i<8 ; ++i)
                if (m_oldCoefs.v[i] > FLT_EPSILON)
                    return false;
        for (int i=0 ; i<8 ; ++i)
            if (m_coefs.v[i] > FLT_EPSILON)
                return false;
        return true;
    }
};

class AudioMatrixStereo
{
    union Coefs
    {
        float v[8][2];
#if __SSE__
        __m128 q[4];
        __m64 d[8];
#endif
    };
    Coefs m_coefs = {};
    Coefs m_oldCoefs = {};
    size_t m_slewFrames = 0;
    size_t m_curSlewFrame = ~size_t(0);
public:
    AudioMatrixStereo() {setDefaultMatrixCoefficients(AudioChannelSet::Stereo);}

    void setDefaultMatrixCoefficients(AudioChannelSet acSet);
    void setMatrixCoefficients(const float coefs[8][2], size_t slewFrames=0)
    {
        m_slewFrames = slewFrames;
#if __SSE__
        if (m_curSlewFrame != 0)
        {
            m_oldCoefs.q[0] = m_coefs.q[0];
            m_oldCoefs.q[1] = m_coefs.q[1];
            m_oldCoefs.q[2] = m_coefs.q[2];
            m_oldCoefs.q[3] = m_coefs.q[3];
        }
        m_coefs.q[0] = _mm_loadu_ps(coefs[0]);
        m_coefs.q[1] = _mm_loadu_ps(coefs[2]);
        m_coefs.q[2] = _mm_loadu_ps(coefs[4]);
        m_coefs.q[3] = _mm_loadu_ps(coefs[6]);
#else
        for (int i=0 ; i<8 ; ++i)
        {
            if (m_curSlewFrame != 0)
            {
                m_oldCoefs.v[i][0] = m_coefs.v[i][0];
                m_oldCoefs.v[i][1] = m_coefs.v[i][1];
            }
            m_coefs.v[i][0] = coefs.v[i][0];
            m_coefs.v[i][1] = coefs.v[i][1];
        }
#endif
        m_curSlewFrame = 0;
    }

    int16_t* mixStereoSampleData(const AudioVoiceEngineMixInfo& info,
                                 const int16_t* dataIn, int16_t* dataOut, size_t frames);
    int32_t* mixStereoSampleData(const AudioVoiceEngineMixInfo& info,
                                 const int32_t* dataIn, int32_t* dataOut, size_t frames);
    float* mixStereoSampleData(const AudioVoiceEngineMixInfo& info,
                               const float* dataIn, float* dataOut, size_t frames);

    bool isSilent() const
    {
        if (m_curSlewFrame < m_slewFrames)
            for (int i=0 ; i<8 ; ++i)
                if (m_oldCoefs.v[i][0] > FLT_EPSILON || m_oldCoefs.v[i][1] > FLT_EPSILON)
                    return false;
        for (int i=0 ; i<8 ; ++i)
            if (m_coefs.v[i][0] > FLT_EPSILON || m_coefs.v[i][1] > FLT_EPSILON)
                return false;
        return true;
    }
};

}

#endif // BOO_AUDIOMATRIX_HPP
