#include "LtRtProcessing.hpp"
#include <cmath>

namespace boo
{

template <typename T>
inline T ClampFull(float in)
{
    if(std::is_floating_point<T>())
    {
        return std::min<T>(std::max<T>(in, -1.f), 1.f);
    }
    else
    {
        constexpr T MAX = std::numeric_limits<T>::max();
        constexpr T MIN = std::numeric_limits<T>::min();

        if (in < MIN)
            return MIN;
        else if (in > MAX)
            return MAX;
        else
            return in;
    }
}

#if INTEL_IPP

WindowedHilbert::WindowedHilbert(int windowSamples)
: m_windowSamples(windowSamples), m_halfSamples(windowSamples / 2),
  m_inputBuf(new Ipp32f[m_windowSamples * 2 + m_halfSamples]),
  m_outputBuf(new Ipp32fc[m_windowSamples * 4]),
  m_hammingTable(new Ipp32f[m_halfSamples])
{
    memset(m_inputBuf.get(), 0, sizeof(Ipp32fc) * m_windowSamples * 2 + m_halfSamples);
    memset(m_outputBuf.get(), 0, sizeof(Ipp32fc) * m_windowSamples * 4);
    m_output[0] = m_outputBuf.get();
    m_output[1] = m_output[0] + m_windowSamples;
    m_output[2] = m_output[1] + m_windowSamples;
    m_output[3] = m_output[2] + m_windowSamples;
    int sizeSpec, sizeBuf;
    ippsHilbertGetSize_32f32fc(m_windowSamples, ippAlgHintNone, &sizeSpec, &sizeBuf);
    m_spec = (IppsHilbertSpec*)ippMalloc(sizeSpec);
    m_buffer = (Ipp8u*)ippMalloc(sizeBuf);
    ippsHilbertInit_32f32fc(m_windowSamples, ippAlgHintNone, m_spec, m_buffer);

    for (int i=0 ; i<m_halfSamples ; ++i)
        m_hammingTable[i] = Ipp32f(std::cos(M_PI * (i / double(m_halfSamples) + 1.0)) * 0.5 + 0.5);
}

WindowedHilbert::~WindowedHilbert()
{
    ippFree(m_spec);
    ippFree(m_buffer);
}

void WindowedHilbert::_AddWindow()
{
    if (m_bufIdx)
    {
        /* Mirror last half of samples to start of input buffer */
        Ipp32f* bufBase = &m_inputBuf[m_windowSamples * 2];
        for (int i=0 ; i<m_halfSamples ; ++i)
            m_inputBuf[i] = bufBase[i];
        ippsHilbert_32f32fc(&m_inputBuf[m_windowSamples],
                            m_output[2], m_spec, m_buffer);
        ippsHilbert_32f32fc(&m_inputBuf[m_windowSamples + m_halfSamples],
                            m_output[3], m_spec, m_buffer);
    }
    else
    {
        ippsHilbert_32f32fc(&m_inputBuf[0],
                            m_output[0], m_spec, m_buffer);
        ippsHilbert_32f32fc(&m_inputBuf[m_halfSamples],
                            m_output[1], m_spec, m_buffer);
    }
    m_bufIdx ^= 1;
}

void WindowedHilbert::AddWindow(const float* input, int stride)
{
    Ipp32f* bufBase = &m_inputBuf[m_windowSamples * m_bufIdx + m_halfSamples];
    for (int i=0 ; i<m_windowSamples ; ++i)
        bufBase[i] = input[i * stride];
    _AddWindow();
}

void WindowedHilbert::AddWindow(const int32_t* input, int stride)
{
    Ipp32f* bufBase = &m_inputBuf[m_windowSamples * m_bufIdx + m_halfSamples];
    for (int i=0 ; i<m_windowSamples ; ++i)
        bufBase[i] = input[i * stride] / (float(INT32_MAX) + 1.f);
    _AddWindow();
}

void WindowedHilbert::AddWindow(const int16_t* input, int stride)
{
    Ipp32f* bufBase = &m_inputBuf[m_windowSamples * m_bufIdx + m_halfSamples];
    for (int i=0 ; i<m_windowSamples ; ++i)
        bufBase[i] = input[i * stride] / (float(INT16_MAX) + 1.f);
    _AddWindow();
}

template <typename T>
void WindowedHilbert::Output(T* output, float lCoef, float rCoef) const
{
    int first, middle, last;
    if (m_bufIdx)
    {
        first = 3;
        middle = 0;
        last = 1;
    }
    else
    {
        first = 1;
        middle = 2;
        last = 3;
    }

    int i, t;
    for (i=0, t=0 ; i<m_halfSamples ; ++i, ++t)
    {
        float tmp = m_output[first][i].im * (1.f - m_hammingTable[t]) +
                    m_output[middle][i].im * m_hammingTable[t];
        output[i*2] = ClampFull<T>(output[i*2] + tmp * lCoef);
        output[i*2+1] = ClampFull<T>(output[i*2+1] + tmp * rCoef);
    }
    for (; i<m_windowSamples-m_halfSamples ; ++i)
    {
        float tmp = m_output[middle][i].im;
        output[i*2] = ClampFull<T>(output[i*2] + tmp * lCoef);
        output[i*2+1] = ClampFull<T>(output[i*2+1] + tmp * rCoef);
    }
    for (t=0 ; i<m_windowSamples ; ++i, ++t)
    {
        float tmp = m_output[middle][i].im * (1.f - m_hammingTable[t]) +
                    m_output[last][i].im * m_hammingTable[t];
        output[i*2] = ClampFull<T>(output[i*2] + tmp * lCoef);
        output[i*2+1] = ClampFull<T>(output[i*2+1] + tmp * rCoef);
    }
}

template void WindowedHilbert::Output<int16_t>(int16_t* output, float lCoef, float rCoef) const;
template void WindowedHilbert::Output<int32_t>(int32_t* output, float lCoef, float rCoef) const;
template void WindowedHilbert::Output<float>(float* output, float lCoef, float rCoef) const;

#endif

template <> int16_t* LtRtProcessing::_getInBuf<int16_t>() { return m_16Buffer.get(); }
template <> int32_t* LtRtProcessing::_getInBuf<int32_t>() { return m_32Buffer.get(); }
template <> float* LtRtProcessing::_getInBuf<float>() { return m_fltBuffer.get(); }

template <> int16_t* LtRtProcessing::_getOutBuf<int16_t>() { return m_16Buffer.get() + m_outputOffset; }
template <> int32_t* LtRtProcessing::_getOutBuf<int32_t>() { return m_32Buffer.get() + m_outputOffset; }
template <> float* LtRtProcessing::_getOutBuf<float>() { return m_fltBuffer.get() + m_outputOffset; }

LtRtProcessing::LtRtProcessing(int _5msFrames, const AudioVoiceEngineMixInfo& mixInfo)
: m_inMixInfo(mixInfo), m_5msFrames(_5msFrames), m_5msFramesHalf(_5msFrames / 2),
  m_outputOffset(m_5msFrames * 5 * 2), m_hilbertSL(_5msFrames), m_hilbertSR(_5msFrames)
{
    m_inMixInfo.m_channels = AudioChannelSet::Surround51;
    m_inMixInfo.m_channelMap.m_channelCount = 5;
    m_inMixInfo.m_channelMap.m_channels[0] = AudioChannel::FrontLeft;
    m_inMixInfo.m_channelMap.m_channels[1] = AudioChannel::FrontRight;
    m_inMixInfo.m_channelMap.m_channels[2] = AudioChannel::FrontCenter;
    m_inMixInfo.m_channelMap.m_channels[3] = AudioChannel::RearLeft;
    m_inMixInfo.m_channelMap.m_channels[4] = AudioChannel::RearRight;

    int samples = m_5msFrames * (5 * 2 + 2 * 2);
    switch (mixInfo.m_sampleFormat)
    {
    case SOXR_INT16_I:
        m_16Buffer.reset(new int16_t[samples]);
        memset(m_16Buffer.get(), 0, sizeof(int16_t) * samples);
        break;
    case SOXR_INT32_I:
        m_32Buffer.reset(new int32_t[samples]);
        memset(m_32Buffer.get(), 0, sizeof(int32_t) * samples);
        break;
    case SOXR_FLOAT32_I:
        m_fltBuffer.reset(new float[samples]);
        memset(m_fltBuffer.get(), 0, sizeof(float) * samples);
        break;
    default:
        break;
    }
}

template <typename T>
void LtRtProcessing::Process(const T* input, T* output, int frameCount)
{
    int outFramesRem = frameCount;
    T* inBuf = _getInBuf<T>();
    T* outBuf = _getOutBuf<T>();
    int tail = std::min(m_5msFrames * 2, m_bufferTail + frameCount);
    int samples = (tail - m_bufferTail) * 5;
    memmove(&inBuf[m_bufferTail * 5], input, samples * sizeof(float));
    input += samples;
    frameCount -= tail - m_bufferTail;

    int bufIdx = m_bufferTail / m_5msFrames;
    if (tail / m_5msFrames > bufIdx)
    {
        T* in = &inBuf[bufIdx * m_5msFrames * 5];
        T* out = &outBuf[bufIdx * m_5msFrames * 2];
        m_hilbertSL.AddWindow(in + 3, 5);
        m_hilbertSR.AddWindow(in + 4, 5);

        // x(:,1) + sqrt(.5)*x(:,3) + sqrt(19/25)*x(:,4) + sqrt(6/25)*x(:,5)
        // x(:,2) + sqrt(.5)*x(:,3) - sqrt(6/25)*x(:,4) - sqrt(19/25)*x(:,5)
        if (bufIdx)
        {
            int delayI = -m_5msFramesHalf;
            for (int i=0 ; i<m_5msFrames ; ++i, ++delayI)
            {
                out[i * 2] = ClampFull<T>(in[delayI * 5] + 0.7071068f * in[delayI * 5 + 2]);
                out[i * 2 + 1] = ClampFull<T>(in[delayI * 5 + 1] + 0.7071068f * in[delayI * 5 + 2]);
            }
        }
        else
        {
            int delayI = m_5msFrames * 2 - m_5msFramesHalf;
            int i;
            for (i=0 ; i<m_5msFramesHalf ; ++i, ++delayI)
            {
                out[i * 2] = ClampFull<T>(in[delayI * 5] + 0.7071068f * in[delayI * 5 + 2]);
                out[i * 2 + 1] = ClampFull<T>(in[delayI * 5 + 1] + 0.7071068f * in[delayI * 5 + 2]);
            }
            delayI = 0;
            for (; i<m_5msFrames ; ++i, ++delayI)
            {
                out[i * 2] = ClampFull<T>(in[delayI * 5] + 0.7071068f * in[delayI * 5 + 2]);
                out[i * 2 + 1] = ClampFull<T>(in[delayI * 5 + 1] + 0.7071068f * in[delayI * 5 + 2]);
            }
        }
#if INTEL_IPP
        m_hilbertSL.Output(out, 0.8717798f, 0.4898979f);
        m_hilbertSR.Output(out, -0.4898979f, -0.8717798f);
#endif
    }
    m_bufferTail = tail;

    if (frameCount)
    {
        samples = frameCount * 5;
        memmove(inBuf, input, samples * sizeof(float));
        m_bufferTail = frameCount;
    }

    int head = std::min(m_5msFrames * 2, m_bufferHead + outFramesRem);
    samples = (head - m_bufferHead) * 2;
    memmove(output, outBuf + m_bufferHead * 2, samples * sizeof(float));
    output += samples;
    outFramesRem -= head - m_bufferHead;
    m_bufferHead = head;
    if (outFramesRem)
    {
        samples = outFramesRem * 2;
        memmove(output, outBuf, samples * sizeof(float));
        m_bufferHead = outFramesRem;
    }
}

template void LtRtProcessing::Process<int16_t>(const int16_t* input, int16_t* output, int frameCount);
template void LtRtProcessing::Process<int32_t>(const int32_t* input, int32_t* output, int frameCount);
template void LtRtProcessing::Process<float>(const float* input, float* output, int frameCount);

}
