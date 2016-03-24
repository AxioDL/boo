#ifndef BOO_AUDIOMATRIX_HPP
#define BOO_AUDIOMATRIX_HPP

#include "boo/audiodev/IAudioVoice.hpp"
#include <vector>
#include <stdint.h>

namespace boo
{
struct AudioVoiceEngineMixInfo;

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
