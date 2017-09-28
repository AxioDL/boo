#ifndef BOO_LTRTPROCESSING_HPP
#define BOO_LTRTPROCESSING_HPP

#include "boo/System.hpp"
#include "boo/audiodev/IAudioVoice.hpp"
#include "Common.hpp"
#include <memory>

#if INTEL_IPP
#include "ipp.h"
#endif

namespace boo
{

#if INTEL_IPP
class WindowedHilbert
{
    IppsHilbertSpec* m_spec;
    Ipp8u* m_buffer;
    int m_windowSamples, m_halfSamples;
    int m_bufIdx = 0;
    int m_bufferTail = 0;
    std::unique_ptr<Ipp32f[]> m_inputBuf;
    std::unique_ptr<Ipp32fc[]> m_outputBuf;
    Ipp32fc* m_output[4];
    std::unique_ptr<Ipp32f[]> m_hammingTable;
    void _AddWindow();
public:
    explicit WindowedHilbert(int windowSamples);
    ~WindowedHilbert();
    void AddWindow(const float* input, int stride);
    void AddWindow(const int32_t* input, int stride);
    void AddWindow(const int16_t* input, int stride);
    template <typename T>
    void Output(T* output, float lCoef, float rCoef) const;
};
#endif

class LtRtProcessing
{
    AudioVoiceEngineMixInfo m_inMixInfo;
    int m_5msFrames;
    int m_5msFramesHalf;
    int m_outputOffset;
    int m_bufferTail = 0;
    int m_bufferHead = 0;
    std::unique_ptr<int16_t[]> m_16Buffer;
    std::unique_ptr<int32_t[]> m_32Buffer;
    std::unique_ptr<float[]> m_fltBuffer;
#if INTEL_IPP
    WindowedHilbert m_hilbertSL, m_hilbertSR;
#endif
    template <typename T> T* _getInBuf();
    template <typename T> T* _getOutBuf();
public:
    LtRtProcessing(int _5msFrames, const AudioVoiceEngineMixInfo& mixInfo);
    template <typename T>
    void Process(const T* input, T* output, int frameCount);
    const AudioVoiceEngineMixInfo& inMixInfo() const { return m_inMixInfo; }
};

}

#endif // BOO_LTRTPROCESSING_HPP
