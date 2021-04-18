#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "boo/audiodev/IAudioSubmix.hpp"
#include "lib/audiodev/Common.hpp"

#if defined(__x86_64__) || defined(_M_AMD64)
#include <immintrin.h>
#elif defined(__aarch64__) || defined(_M_ARM64)
#define __SSE__ 1
#include "sse2neon.h"
#endif

struct AudioUnitVoiceEngine;
struct VSTVoiceEngine;
struct WAVOutVoiceEngine;

namespace boo {
class BaseAudioVoiceEngine;
class AudioVoice;
struct AudioVoiceEngineMixInfo;
/* Output gains for each mix-send/channel */

class AudioSubmix : public ListNode<AudioSubmix, BaseAudioVoiceEngine*, IAudioSubmix> {
  friend class BaseAudioVoiceEngine;
  friend class AudioVoiceMono;
  friend class AudioVoiceStereo;
  friend struct WASAPIAudioVoiceEngine;
  friend struct ::AudioUnitVoiceEngine;
  friend struct ::VSTVoiceEngine;
  friend struct ::WAVOutVoiceEngine;

  /* Mixer-engine relationships */
  int m_busId;
  bool m_mainOut;

  /* Callback (effect source, optional) */
  IAudioSubmixCallback* m_cb;

  /* Slew state for output gains */
  size_t m_slewFrames = 0;
  size_t m_curSlewFrame = 0;

  /* Output gains for each mix-send/channel */
  std::unordered_map<IAudioSubmix*, std::array<float, 2>> m_sendGains;

  /* Temporary scratch buffers for accumulating submix audio */
  std::vector<int16_t> m_scratch16;
  std::vector<int32_t> m_scratch32;
  std::vector<float> m_scratchFlt;
  template <typename T>
  std::vector<T>& _getScratch();

  /* Override scratch buffers with alternate destination */
  int16_t* m_redirect16 = nullptr;
  int32_t* m_redirect32 = nullptr;
  float* m_redirectFlt = nullptr;
  template <typename T>
  T*& _getRedirect();

  /* C3-linearization support (to mitigate a potential diamond problem on 'clever' submix routes) */
  bool _isDirectDependencyOf(AudioSubmix* send);
  std::list<AudioSubmix*> _linearizeC3();
  static bool _mergeC3(std::list<AudioSubmix*>& output, std::vector<std::list<AudioSubmix*>>& lists);

  /* Fill scratch buffers with silence for new mix cycle */
  template <typename T>
  void _zeroFill();

  /* Receive audio from a single voice / submix */
  template <typename T>
  T* _getMergeBuf(size_t frames);

  /* Mix scratch buffers into sends */
  template <typename T>
  size_t _pumpAndMix(size_t frames);

  void _resetOutputSampleRate();

public:
  static AudioSubmix*& _getHeadPtr(BaseAudioVoiceEngine* head);
  static std::unique_lock<std::recursive_mutex> _getHeadLock(BaseAudioVoiceEngine* head);

  AudioSubmix(BaseAudioVoiceEngine& root, IAudioSubmixCallback* cb, int busId, bool mainOut);
  ~AudioSubmix() override;

  void resetSendLevels() override;
  void setSendLevel(IAudioSubmix* submix, float level, bool slew) override;
  const AudioVoiceEngineMixInfo& mixInfo() const;
  double getSampleRate() const override;
  SubmixFormat getSampleFormat() const override;
};

template <>
inline std::vector<int16_t>& AudioSubmix::_getScratch() {
  return m_scratch16;
}
template <>
inline std::vector<int32_t>& AudioSubmix::_getScratch() {
  return m_scratch32;
}
template <>
inline std::vector<float>& AudioSubmix::_getScratch() {
  return m_scratchFlt;
}

template <>
inline int16_t*& AudioSubmix::_getRedirect<int16_t>() {
  return m_redirect16;
}
template <>
inline int32_t*& AudioSubmix::_getRedirect<int32_t>() {
  return m_redirect32;
}
template <>
inline float*& AudioSubmix::_getRedirect<float>() {
  return m_redirectFlt;
}

} // namespace boo
