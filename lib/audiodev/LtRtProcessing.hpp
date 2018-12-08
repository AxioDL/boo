#pragma once

#include "boo/System.hpp"
#include "boo/audiodev/IAudioVoice.hpp"
#include "Common.hpp"
#include <memory>

#if INTEL_IPP
#include "ipp.h"
#endif

namespace boo {

#if INTEL_IPP
#define USE_LPF 0

#if USE_LPF
class FIRFilter12k {
  IppsFIRSpec_32f* m_firSpec;
  Ipp8u* m_firBuffer;
  Ipp32f* m_dlySrc;
  Ipp32f* m_inBuf;

public:
  explicit FIRFilter12k(int windowFrames, double sampleRate);
  ~FIRFilter12k();
  void Process(Ipp32f* buf, int windowFrames);
};
#endif

class WindowedHilbert {
#if USE_LPF
  FIRFilter12k m_fir;
#endif
  IppsHilbertSpec* m_spec;
  Ipp8u* m_buffer;
  int m_windowFrames, m_halfFrames;
  int m_bufIdx = 0;
  Ipp32f* m_inputBuf;
  Ipp32fc* m_outputBuf;
  Ipp32fc* m_output[4];
  Ipp32f* m_hammingTable;
  void _AddWindow();

public:
  explicit WindowedHilbert(int windowFrames, double sampleRate);
  ~WindowedHilbert();
  void AddWindow(const float* input, int stride);
  void AddWindow(const int32_t* input, int stride);
  void AddWindow(const int16_t* input, int stride);
  template <typename T>
  void Output(T* output, float lCoef, float rCoef) const;
};
#endif

class LtRtProcessing {
  AudioVoiceEngineMixInfo m_inMixInfo;
  int m_windowFrames;
  int m_halfFrames;
  int m_outputOffset;
  int m_bufferTail = 0;
  int m_bufferHead = 0;
  std::unique_ptr<int16_t[]> m_16Buffer;
  std::unique_ptr<int32_t[]> m_32Buffer;
  std::unique_ptr<float[]> m_fltBuffer;
#if INTEL_IPP
  WindowedHilbert m_hilbertSL, m_hilbertSR;
#endif
  template <typename T>
  T* _getInBuf();
  template <typename T>
  T* _getOutBuf();

public:
  LtRtProcessing(int _5msFrames, const AudioVoiceEngineMixInfo& mixInfo);
  template <typename T>
  void Process(const T* input, T* output, int frameCount);
  const AudioVoiceEngineMixInfo& inMixInfo() const { return m_inMixInfo; }
};

} // namespace boo
