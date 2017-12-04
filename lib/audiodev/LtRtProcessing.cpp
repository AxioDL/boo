#include "LtRtProcessing.hpp"
#include <cmath>
#include <algorithm>

#undef min
#undef max

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

#ifndef M_PI
#define M_PI 3.14159265358979323846 /* pi */
#endif

#if USE_LPF
static constexpr int FirTaps = 27;

FIRFilter12k::FIRFilter12k(int windowFrames, double sampleRate)
{
    Ipp64f* taps = ippsMalloc_64f(FirTaps);
    Ipp32f* taps32 = ippsMalloc_32f(FirTaps);
    int sizeSpec, sizeBuf;

    ippsFIRGenGetBufferSize(FirTaps, &sizeBuf);
    m_firBuffer = ippsMalloc_8u(sizeBuf);
    ippsFIRGenLowpass_64f(12000.0 / sampleRate, taps, FirTaps, ippWinBartlett, ippTrue, m_firBuffer);
    ippsConvert_64f32f(taps, taps32, FirTaps);
    ippsFree(taps);
    ippsFree(m_firBuffer);

    m_dlySrc = ippsMalloc_32f(FirTaps);

    ippsFIRSRGetSize(FirTaps, ipp32f, &sizeSpec, &sizeBuf);
    m_firSpec = (IppsFIRSpec_32f*)ippsMalloc_8u(sizeSpec);
    m_firBuffer = ippsMalloc_8u(sizeBuf);
    ippsFIRSRInit_32f(taps32, FirTaps, ippAlgDirect, m_firSpec);
    ippsFree(taps32);

    m_inBuf = ippsMalloc_32f(windowFrames);
}

FIRFilter12k::~FIRFilter12k()
{
    ippsFree(m_firSpec);
    ippsFree(m_firBuffer);
    ippsFree(m_dlySrc);
    ippsFree(m_inBuf);
}

void FIRFilter12k::Process(Ipp32f* buf, int windowFrames)
{
    ippsZero_32f(m_dlySrc, FirTaps);
    ippsMove_32f(buf, m_inBuf, windowFrames);
    ippsFIRSR_32f(m_inBuf, buf, windowFrames, m_firSpec, m_dlySrc, nullptr, m_firBuffer);
}
#endif

WindowedHilbert::WindowedHilbert(int windowFrames, double sampleRate) :
#if USE_LPF
  m_fir(windowFrames, sampleRate),
#endif
  m_windowFrames(windowFrames),
  m_halfFrames(windowFrames / 2),
  m_inputBuf(ippsMalloc_32f(m_windowFrames * 2 + m_halfFrames)),
  m_outputBuf(ippsMalloc_32fc(m_windowFrames * 4)),
  m_hammingTable(ippsMalloc_32f(m_halfFrames))
{
    ippsZero_32f(m_inputBuf, m_windowFrames * 2 + m_halfFrames);
    ippsZero_32fc(m_outputBuf, m_windowFrames * 4);
    m_output[0] = m_outputBuf;
    m_output[1] = m_output[0] + m_windowFrames;
    m_output[2] = m_output[1] + m_windowFrames;
    m_output[3] = m_output[2] + m_windowFrames;
    int sizeSpec, sizeBuf;
    ippsHilbertGetSize_32f32fc(m_windowFrames, ippAlgHintFast, &sizeSpec, &sizeBuf);
    m_spec = (IppsHilbertSpec*)ippMalloc(sizeSpec);
    m_buffer = (Ipp8u*)ippMalloc(sizeBuf);
    ippsHilbertInit_32f32fc(m_windowFrames, ippAlgHintFast, m_spec, m_buffer);

    for (int i=0 ; i<m_halfFrames ; ++i)
        m_hammingTable[i] = Ipp32f(std::cos(M_PI * (i / double(m_halfFrames) + 1.0)) * 0.5 + 0.5);
}

WindowedHilbert::~WindowedHilbert()
{
    ippFree(m_spec);
    ippFree(m_buffer);
    ippsFree(m_inputBuf);
    ippsFree(m_outputBuf);
    ippsFree(m_hammingTable);
}

void WindowedHilbert::_AddWindow()
{
#if USE_LPF
    Ipp32f* inBufBase = &m_inputBuf[m_windowFrames * m_bufIdx + m_halfFrames];
    m_fir.Process(inBufBase, m_windowFrames);
#endif

    if (m_bufIdx)
    {
        /* Mirror last half of samples to start of input buffer */
        Ipp32f* bufBase = &m_inputBuf[m_windowFrames * 2];
        ippsCopy_32f(bufBase, m_inputBuf, m_halfFrames);
        ippsHilbert_32f32fc(&m_inputBuf[m_windowFrames],
                            m_output[2], m_spec, m_buffer);
        ippsHilbert_32f32fc(&m_inputBuf[m_windowFrames + m_halfFrames],
                            m_output[3], m_spec, m_buffer);
    }
    else
    {
        ippsHilbert_32f32fc(&m_inputBuf[0],
                            m_output[0], m_spec, m_buffer);
        ippsHilbert_32f32fc(&m_inputBuf[m_halfFrames],
                            m_output[1], m_spec, m_buffer);
    }
    m_bufIdx ^= 1;
}

void WindowedHilbert::AddWindow(const float* input, int stride)
{
    Ipp32f* bufBase = &m_inputBuf[m_windowFrames * m_bufIdx + m_halfFrames];
    for (int i=0 ; i<m_windowFrames ; ++i)
        bufBase[i] = input[i * stride];
    _AddWindow();
}

void WindowedHilbert::AddWindow(const int32_t* input, int stride)
{
    Ipp32f* bufBase = &m_inputBuf[m_windowFrames * m_bufIdx + m_halfFrames];
    for (int i=0 ; i<m_windowFrames ; ++i)
        bufBase[i] = input[i * stride] / (float(INT32_MAX) + 1.f);
    _AddWindow();
}

void WindowedHilbert::AddWindow(const int16_t* input, int stride)
{
    Ipp32f* bufBase = &m_inputBuf[m_windowFrames * m_bufIdx + m_halfFrames];
    for (int i=0 ; i<m_windowFrames ; ++i)
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

#if 0
    for (int i=0 ; i<m_windowFrames ; ++i)
    {
        float tmp = m_output[middle][i].im;
        output[i*2] = ClampFull<T>(output[i*2] + tmp * lCoef);
        output[i*2+1] = ClampFull<T>(output[i*2+1] + tmp * rCoef);
    }
    return;
#endif

    int i, t;
    for (i=0, t=0 ; i<m_halfFrames ; ++i, ++t)
    {
        float tmp = m_output[first][m_halfFrames + i].im * (1.f - m_hammingTable[t]) +
                    m_output[middle][i].im * m_hammingTable[t];
        output[i*2] = ClampFull<T>(output[i*2] + tmp * lCoef);
        output[i*2+1] = ClampFull<T>(output[i*2+1] + tmp * rCoef);
    }
    for (; i<m_windowFrames-m_halfFrames ; ++i)
    {
        float tmp = m_output[middle][i].im;
        output[i*2] = ClampFull<T>(output[i*2] + tmp * lCoef);
        output[i*2+1] = ClampFull<T>(output[i*2+1] + tmp * rCoef);
    }
    for (t=0 ; i<m_windowFrames ; ++i, ++t)
    {
        float tmp = m_output[middle][i].im * (1.f - m_hammingTable[t]) +
                    m_output[last][t].im * m_hammingTable[t];
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
: m_inMixInfo(mixInfo), m_windowFrames(_5msFrames * 4), m_halfFrames(m_windowFrames / 2),
  m_outputOffset(m_windowFrames * 5 * 2)
#if INTEL_IPP
, m_hilbertSL(m_windowFrames, mixInfo.m_sampleRate),
  m_hilbertSR(m_windowFrames, mixInfo.m_sampleRate)
#endif
{
    m_inMixInfo.m_channels = AudioChannelSet::Surround51;
    m_inMixInfo.m_channelMap.m_channelCount = 5;
    m_inMixInfo.m_channelMap.m_channels[0] = AudioChannel::FrontLeft;
    m_inMixInfo.m_channelMap.m_channels[1] = AudioChannel::FrontRight;
    m_inMixInfo.m_channelMap.m_channels[2] = AudioChannel::FrontCenter;
    m_inMixInfo.m_channelMap.m_channels[3] = AudioChannel::RearLeft;
    m_inMixInfo.m_channelMap.m_channels[4] = AudioChannel::RearRight;

    int samples = m_windowFrames * (5 * 2 + 2 * 2);
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
#if 0
    for (int i=0 ; i<frameCount ; ++i)
    {
        output[i * 2] = input[i * 5 + 3];
        output[i * 2 + 1] = input[i * 5 + 4];
    }
    return;
#endif

    int outFramesRem = frameCount;
    T* inBuf = _getInBuf<T>();
    T* outBuf = _getOutBuf<T>();
    int tail = std::min(m_windowFrames * 2, m_bufferTail + frameCount);
    int samples = (tail - m_bufferTail) * 5;
    memmove(&inBuf[m_bufferTail * 5], input, samples * sizeof(T));
    //printf("input %d to %d\n", tail - m_bufferTail, m_bufferTail);
    input += samples;
    frameCount -= tail - m_bufferTail;

    int head = std::min(m_windowFrames * 2, m_bufferHead + outFramesRem);
    samples = (head - m_bufferHead) * 2;
    memmove(output, outBuf + m_bufferHead * 2, samples * sizeof(T));
    //printf("output %d from %d\n", head - m_bufferHead, m_bufferHead);
    output += samples;
    outFramesRem -= head - m_bufferHead;

    int bufIdx = m_bufferTail / m_windowFrames;
    if (tail / m_windowFrames > bufIdx)
    {
        T* in = &inBuf[bufIdx * m_windowFrames * 5];
        T* out = &outBuf[bufIdx * m_windowFrames * 2];
#if INTEL_IPP
        m_hilbertSL.AddWindow(in + 3, 5);
        m_hilbertSR.AddWindow(in + 4, 5);
#endif

        // x(:,1) + sqrt(.5)*x(:,3) + sqrt(19/25)*x(:,4) + sqrt(6/25)*x(:,5)
        // x(:,2) + sqrt(.5)*x(:,3) - sqrt(6/25)*x(:,4) - sqrt(19/25)*x(:,5)
        if (bufIdx)
        {
            int delayI = -m_halfFrames;
            for (int i=0 ; i<m_windowFrames ; ++i, ++delayI)
            {
                out[i * 2] = ClampFull<T>(in[delayI * 5] + 0.7071068f * in[delayI * 5 + 2]);
                out[i * 2 + 1] = ClampFull<T>(in[delayI * 5 + 1] + 0.7071068f * in[delayI * 5 + 2]);
                //printf("in %d out %d\n", bufIdx * m_5msFrames + delayI, bufIdx * m_5msFrames + i);
            }
        }
        else
        {
            int delayI = m_windowFrames * 2 - m_halfFrames;
            int i;
            for (i=0 ; i<m_halfFrames ; ++i, ++delayI)
            {
                out[i * 2] = ClampFull<T>(in[delayI * 5] + 0.7071068f * in[delayI * 5 + 2]);
                out[i * 2 + 1] = ClampFull<T>(in[delayI * 5 + 1] + 0.7071068f * in[delayI * 5 + 2]);
                //printf("in %d out %d\n", bufIdx * m_5msFrames + delayI, bufIdx * m_5msFrames + i);
            }
            delayI = 0;
            for (; i<m_windowFrames ; ++i, ++delayI)
            {
                out[i * 2] = ClampFull<T>(in[delayI * 5] + 0.7071068f * in[delayI * 5 + 2]);
                out[i * 2 + 1] = ClampFull<T>(in[delayI * 5 + 1] + 0.7071068f * in[delayI * 5 + 2]);
                //printf("in %d out %d\n", bufIdx * m_5msFrames + delayI, bufIdx * m_5msFrames + i);
            }
        }
#if INTEL_IPP
        m_hilbertSL.Output(out, 0.8717798f, 0.4898979f);
        m_hilbertSR.Output(out, -0.4898979f, -0.8717798f);
#endif
    }
    m_bufferTail = (tail == m_windowFrames * 2) ? 0 : tail;
    m_bufferHead = (head == m_windowFrames * 2) ? 0 : head;

    if (frameCount)
    {
        samples = frameCount * 5;
        memmove(inBuf, input, samples * sizeof(T));
        //printf("input %d to %d\n", frameCount, 0);
        m_bufferTail = frameCount;
    }

    if (outFramesRem)
    {
        samples = outFramesRem * 2;
        memmove(output, outBuf, samples * sizeof(T));
        //printf("output %d from %d\n", outFramesRem, 0);
        m_bufferHead = outFramesRem;
    }
}

template void LtRtProcessing::Process<int16_t>(const int16_t* input, int16_t* output, int frameCount);
template void LtRtProcessing::Process<int32_t>(const int32_t* input, int32_t* output, int frameCount);
template void LtRtProcessing::Process<float>(const float* input, float* output, int frameCount);

}
