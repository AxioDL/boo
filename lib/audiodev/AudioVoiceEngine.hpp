#pragma once

#include "boo/audiodev/IAudioVoiceEngine.hpp"
#include "LtRtProcessing.hpp"
#include "Common.hpp"
#include "AudioVoice.hpp"
#include "AudioSubmix.hpp"
#include <functional>
#include <mutex>

namespace boo {

/** Base class for managing mixing and sample-rate-conversion amongst active voices */
class BaseAudioVoiceEngine : public IAudioVoiceEngine {
protected:
  friend class AudioVoice;
  friend class AudioSubmix;
  friend class AudioVoiceMono;
  friend class AudioVoiceStereo;
  float m_totalVol = 1.f;
  AudioVoiceEngineMixInfo m_mixInfo;
  std::recursive_mutex m_dataMutex;
  AudioVoice* m_voiceHead = nullptr;
  AudioSubmix* m_submixHead = nullptr;
  size_t m_5msFrames = 0;
  IAudioVoiceEngineCallback* m_engineCallback = nullptr;

  /* Shared scratch buffers for accumulating audio data for resampling */
  std::vector<int16_t> m_scratchIn;
  std::vector<int16_t> m_scratch16Pre;
  std::vector<int32_t> m_scratch32Pre;
  std::vector<float> m_scratchFltPre;
  template <typename T>
  std::vector<T>& _getScratchPre();
  std::vector<int16_t> m_scratch16Post;
  std::vector<int32_t> m_scratch32Post;
  std::vector<float> m_scratchFltPost;
  template <typename T>
  std::vector<T>& _getScratchPost();

  /* LtRt processing if enabled */
  std::unique_ptr<LtRtProcessing> m_ltRtProcessing;
  std::vector<int16_t> m_ltRtIn16;
  std::vector<int32_t> m_ltRtIn32;
  std::vector<float> m_ltRtInFlt;
  template <typename T>
  std::vector<T>& _getLtRtIn();

  std::unique_ptr<AudioSubmix> m_mainSubmix;
  std::list<AudioSubmix*> m_linearizedSubmixes;
  bool m_submixesDirty = true;

  template <typename T>
  void _pumpAndMixVoices(size_t frames, T* dataOut);

  void _resetSampleRate();

public:
  BaseAudioVoiceEngine() : m_mainSubmix(std::make_unique<AudioSubmix>(*this, nullptr, -1, false)) {}
  ~BaseAudioVoiceEngine() override;
  ObjToken<IAudioVoice> allocateNewMonoVoice(double sampleRate, IAudioVoiceCallback* cb,
                                             bool dynamicPitch = false) override;

  ObjToken<IAudioVoice> allocateNewStereoVoice(double sampleRate, IAudioVoiceCallback* cb,
                                               bool dynamicPitch = false) override;

  ObjToken<IAudioSubmix> allocateNewSubmix(bool mainOut, IAudioSubmixCallback* cb, int busId) override;

  void setCallbackInterface(IAudioVoiceEngineCallback* cb) override;

  void setVolume(float vol) override;
  bool enableLtRt(bool enable) override;
  const AudioVoiceEngineMixInfo& mixInfo() const;
  const AudioVoiceEngineMixInfo& clientMixInfo() const;
  AudioChannelSet getAvailableSet() override { return clientMixInfo().m_channels; }
  void pumpAndMixVoices() override {}
  size_t get5MsFrames() const override { return m_5msFrames; }
};

template <>
inline std::vector<int16_t>& BaseAudioVoiceEngine::_getScratchPre<int16_t>() {
  return m_scratch16Pre;
}
template <>
inline std::vector<int32_t>& BaseAudioVoiceEngine::_getScratchPre<int32_t>() {
  return m_scratch32Pre;
}
template <>
inline std::vector<float>& BaseAudioVoiceEngine::_getScratchPre<float>() {
  return m_scratchFltPre;
}

template <>
inline std::vector<int16_t>& BaseAudioVoiceEngine::_getScratchPost<int16_t>() {
  return m_scratch16Post;
}
template <>
inline std::vector<int32_t>& BaseAudioVoiceEngine::_getScratchPost<int32_t>() {
  return m_scratch32Post;
}
template <>
inline std::vector<float>& BaseAudioVoiceEngine::_getScratchPost<float>() {
  return m_scratchFltPost;
}

template <>
inline std::vector<int16_t>& BaseAudioVoiceEngine::_getLtRtIn<int16_t>() {
  return m_ltRtIn16;
}
template <>
inline std::vector<int32_t>& BaseAudioVoiceEngine::_getLtRtIn<int32_t>() {
  return m_ltRtIn32;
}
template <>
inline std::vector<float>& BaseAudioVoiceEngine::_getLtRtIn<float>() {
  return m_ltRtInFlt;
}

} // namespace boo
