#ifndef BOO_AUDIOMATRIX_HPP
#define BOO_AUDIOMATRIX_HPP

#include "boo/audiodev/IAudioVoice.hpp"
#include <vector>
#include <stdint.h>
#include <limits.h>

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

static inline float ClampFlt(float in)
{
    if (in < -1.f)
        return -1.f;
    else if (in > 1.f)
        return 1.f;
    return in;
}

class AudioMatrixMono
{
    float m_coefs[8] = {};
    float m_oldCoefs[8] = {};
    size_t m_slewFrames = 0;
    size_t m_curSlewFrame = 0;
public:
    AudioMatrixMono() {setDefaultMatrixCoefficients(AudioChannelSet::Stereo);}

    void setDefaultMatrixCoefficients(AudioChannelSet acSet);
    void setMatrixCoefficients(const float coefs[8], size_t slewFrames=0)
    {
        m_slewFrames = slewFrames;
        m_curSlewFrame = 0;
        for (int i=0 ; i<8 ; ++i)
        {
            m_oldCoefs[i] = m_coefs[i];
            m_coefs[i] = coefs[i];
        }
    }

    int16_t* mixMonoSampleData(const AudioVoiceEngineMixInfo& info,
                               const int16_t* dataIn, int16_t* dataOut, size_t samples);
    int32_t* mixMonoSampleData(const AudioVoiceEngineMixInfo& info,
                               const int32_t* dataIn, int32_t* dataOut, size_t samples);
    float* mixMonoSampleData(const AudioVoiceEngineMixInfo& info,
                             const float* dataIn, float* dataOut, size_t samples);
};

class AudioMatrixStereo
{
    float m_coefs[8][2] = {};
    float m_oldCoefs[8][2] = {};
    size_t m_slewFrames = 0;
    size_t m_curSlewFrame = 0;
public:
    AudioMatrixStereo() {setDefaultMatrixCoefficients(AudioChannelSet::Stereo);}

    void setDefaultMatrixCoefficients(AudioChannelSet acSet);
    void setMatrixCoefficients(const float coefs[8][2], size_t slewFrames=0)
    {
        m_slewFrames = slewFrames;
        m_curSlewFrame = 0;
        for (int i=0 ; i<8 ; ++i)
        {
            m_oldCoefs[i][0] = m_coefs[i][0];
            m_oldCoefs[i][1] = m_coefs[i][1];
            m_coefs[i][0] = coefs[i][0];
            m_coefs[i][1] = coefs[i][1];
        }
    }

    int16_t* mixStereoSampleData(const AudioVoiceEngineMixInfo& info,
                                 const int16_t* dataIn, int16_t* dataOut, size_t frames);
    int32_t* mixStereoSampleData(const AudioVoiceEngineMixInfo& info,
                                 const int32_t* dataIn, int32_t* dataOut, size_t frames);
    float* mixStereoSampleData(const AudioVoiceEngineMixInfo& info,
                               const float* dataIn, float* dataOut, size_t frames);
};

}

#endif // BOO_AUDIOMATRIX_HPP
