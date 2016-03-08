#ifndef BOO_AUDIOMATRIX_HPP
#define BOO_AUDIOMATRIX_HPP

#include "IAudioVoice.hpp"
#include <vector>
#include <stdint.h>

namespace boo
{

class AudioMatrixMono
{
    AudioChannelSet m_setOut = AudioChannelSet::Stereo;
    float m_coefs[8];
    std::vector<int16_t> m_interleaveBuf;
public:
    AudioMatrixMono() {setDefaultMatrixCoefficients();}

    AudioChannelSet audioChannelSet() const {return m_setOut;}
    void setAudioChannelSet(AudioChannelSet set) {m_setOut = set;}
    void setDefaultMatrixCoefficients();
    void setMatrixCoefficients(const float coefs[8])
    {
        for (int i=0 ; i<8 ; ++i)
            m_coefs[i] = coefs[i];
    }

    void bufferMonoSampleData(IAudioVoice& voice, const int16_t* data, size_t samples);
};

class AudioMatrixStereo
{
    AudioChannelSet m_setOut = AudioChannelSet::Stereo;
    float m_coefs[8][2];
    std::vector<int16_t> m_interleaveBuf;
public:
    AudioMatrixStereo() {setDefaultMatrixCoefficients();}

    AudioChannelSet audioChannelSet() const {return m_setOut;}
    void setAudioChannelSet(AudioChannelSet set) {m_setOut = set;}
    void setDefaultMatrixCoefficients();
    void setMatrixCoefficients(const float coefs[8][2])
    {
        for (int i=0 ; i<8 ; ++i)
        {
            m_coefs[i][0] = coefs[i][0];
            m_coefs[i][1] = coefs[i][1];
        }
    }

    void bufferStereoSampleData(IAudioVoice& voice, const int16_t* data, size_t frames);
};

}

#endif // BOO_AUDIOMATRIX_HPP
