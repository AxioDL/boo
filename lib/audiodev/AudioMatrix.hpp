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
    float m_coefs[8];
public:
    AudioMatrixMono() {setDefaultMatrixCoefficients(AudioChannelSet::Stereo);}

    void setDefaultMatrixCoefficients(AudioChannelSet acSet);
    void setMatrixCoefficients(const float coefs[8])
    {
        for (int i=0 ; i<8 ; ++i)
            m_coefs[i] = coefs[i];
    }

    void mixMonoSampleData(const AudioVoiceEngineMixInfo& info,
                           const int16_t* dataIn, int16_t* dataOut, size_t samples) const;
    void mixMonoSampleData(const AudioVoiceEngineMixInfo& info,
                           const int32_t* dataIn, int32_t* dataOut, size_t samples) const;
    void mixMonoSampleData(const AudioVoiceEngineMixInfo& info,
                           const float* dataIn, float* dataOut, size_t samples) const;
};

class AudioMatrixStereo
{
    float m_coefs[8][2];
public:
    AudioMatrixStereo() {setDefaultMatrixCoefficients(AudioChannelSet::Stereo);}

    void setDefaultMatrixCoefficients(AudioChannelSet acSet);
    void setMatrixCoefficients(const float coefs[8][2])
    {
        for (int i=0 ; i<8 ; ++i)
        {
            m_coefs[i][0] = coefs[i][0];
            m_coefs[i][1] = coefs[i][1];
        }
    }

    void mixStereoSampleData(const AudioVoiceEngineMixInfo& info,
                             const int16_t* dataIn, int16_t* dataOut, size_t frames) const;
    void mixStereoSampleData(const AudioVoiceEngineMixInfo& info,
                             const int32_t* dataIn, int32_t* dataOut, size_t frames) const;
    void mixStereoSampleData(const AudioVoiceEngineMixInfo& info,
                             const float* dataIn, float* dataOut, size_t frames) const;
};

}

#endif // BOO_AUDIOMATRIX_HPP
