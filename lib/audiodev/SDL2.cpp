#include "lib/audiodev/AudioVoiceEngine.hpp"
#include "logvisor/logvisor.hpp"

#include <SDL_audio.h>
#include <SDL_hints.h>

#include <optional>

#ifdef __GNUC__
[[noreturn]] inline __attribute__((always_inline)) void unreachable() { __builtin_unreachable(); }
#elif defined(_MSC_VER)
[[noreturn]] __forceinline void unreachable() { __assume(false); }
#else
#error Unknown compiler
#endif

namespace boo {
static logvisor::Module Log("boo::SDL2AudioVoiceEngine");

static void SDLAudioCallback(void* userdata, Uint8* stream, int len);

static inline soxr_datatype_t _toSoxrFormat(SDL_AudioFormat format) {
  switch (format) {
  case AUDIO_F32SYS:
    return SOXR_FLOAT32_I;
  case AUDIO_S32SYS:
    return SOXR_INT32_I;
  case AUDIO_S16SYS:
    return SOXR_INT16_I;
  default:
    Log.report(logvisor::Fatal, FMT_STRING("Unhandled audio format {}"), format);
    unreachable();
  }
}

static inline unsigned int _bitsPerSample(SDL_AudioFormat format) {
  switch (format) {
  case AUDIO_F32SYS:
  case AUDIO_S32SYS:
    return 32;
  case AUDIO_S16SYS:
    return 16;
  default:
    Log.report(logvisor::Fatal, FMT_STRING("Unhandled audio format {}"), format);
    unreachable();
  }
}

struct SDL2AudioVoiceEngine : BaseAudioVoiceEngine {
  SDL_AudioDeviceID dev = 0;

  bool _openAudioDevice(const char* name) {
    if (dev != 0) {
      SDL_CloseAudioDevice(dev);
    }
    SDL_AudioSpec desired;
    SDL_AudioSpec obtained;
    SDL_zero(desired);
    SDL_zero(obtained);
    desired.freq = 48000;
    desired.format = AUDIO_F32SYS;
    desired.channels = 2;
    desired.samples = 800; // 16.67ms @ 48000hz
    desired.callback = &SDLAudioCallback;
    desired.userdata = this;
    dev = SDL_OpenAudioDevice(name, 0, &desired, &obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (dev == 0) {
      Log.report(logvisor::Error, FMT_STRING("Failed to open audio device: {}"), SDL_GetError());
      return false;
    }
    m_mixInfo.m_sampleRate = obtained.freq;
    m_mixInfo.m_sampleFormat = _toSoxrFormat(obtained.format);
    m_mixInfo.m_bitsPerSample = _bitsPerSample(obtained.format);
    m_mixInfo.m_channels = AudioChannelSet::Stereo; // TODO
    m_mixInfo.m_periodFrames = obtained.samples;
    m_5msFrames = m_mixInfo.m_sampleRate * 5 / 1000;
    return true;
  }

  SDL2AudioVoiceEngine(const char* uniqueName, const char* friendlyName) {
    Log.report(logvisor::Info, FMT_STRING("Using audio driver {}"), SDL_GetCurrentAudioDriver());
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_APP_NAME, friendlyName);
    _openAudioDevice(nullptr);
  }

  ~SDL2AudioVoiceEngine() override { SDL_CloseAudioDevice(dev); }

  std::string getCurrentAudioOutput() const override {
    // TODO
    return "";
  }

  bool setCurrentAudioOutput(const char* name) override { return _openAudioDevice(name); }

  std::vector<std::pair<std::string, std::string>> enumerateAudioOutputs() const override {
    int numDevices = SDL_GetNumAudioDevices(0);
    if (numDevices == -1) {
      return {};
    }
    std::vector<std::pair<std::string, std::string>> ret;
    for (int i = 0; i < numDevices; ++i) {
      ret.emplace_back(SDL_GetAudioDeviceName(i, 0), std::string{});
    }
    return ret;
  }

  // Called from SDL audio thread; synchronization is handled via lockPump/unlockPump
  void _asyncPumpAndMixVoices(Uint8* stream, int len) {
    const auto channels = m_mixInfo.m_channelMap.m_channelCount;
    switch (m_mixInfo.m_sampleFormat) {
    case SOXR_INT16_I:
      _pumpAndMixVoices(len / (sizeof(int16_t) * channels), reinterpret_cast<int16_t*>(stream));
      break;
    case SOXR_INT32_I:
      _pumpAndMixVoices(len / (sizeof(int32_t) * channels), reinterpret_cast<int32_t*>(stream));
      break;
    case SOXR_FLOAT32_I:
      _pumpAndMixVoices(len / (sizeof(float) * channels), reinterpret_cast<float*>(stream));
      break;
    default:
      Log.report(logvisor::Fatal, FMT_STRING("Unhandled audio format {}"), m_mixInfo.m_sampleFormat);
      unreachable();
    }
  }

  void stopPump() const override { SDL_PauseAudioDevice(dev, 1); }
  void startPump() const override { SDL_PauseAudioDevice(dev, 0); }
  void lockPump() const override { SDL_LockAudioDevice(dev); }
  void unlockPump() const override { SDL_UnlockAudioDevice(dev); }

  // MIDI unsupported
  std::vector<std::pair<std::string, std::string>> enumerateMIDIInputs() const override { return {}; }
  bool supportsVirtualMIDIIn() const override { return false; }
  std::unique_ptr<IMIDIIn> newVirtualMIDIIn(ReceiveFunctor&& receiver) override { return {}; }
  std::unique_ptr<IMIDIOut> newVirtualMIDIOut() override { return {}; }
  std::unique_ptr<IMIDIInOut> newVirtualMIDIInOut(ReceiveFunctor&& receiver) override { return {}; }
  std::unique_ptr<IMIDIIn> newRealMIDIIn(const char* name, ReceiveFunctor&& receiver) override { return {}; }
  std::unique_ptr<IMIDIOut> newRealMIDIOut(const char* name) override { return {}; }
  std::unique_ptr<IMIDIInOut> newRealMIDIInOut(const char* name, ReceiveFunctor&& receiver) override { return {}; }
  bool useMIDILock() const override { return false; }
};

static void SDLAudioCallback(void* userdata, Uint8* stream, int len) {
  auto* engine = static_cast<SDL2AudioVoiceEngine*>(userdata);
  engine->_asyncPumpAndMixVoices(stream, len);
}

std::unique_ptr<IAudioVoiceEngine> NewAudioVoiceEngine(const char* uniqueName, const char* friendlyName) {
  return std::make_unique<SDL2AudioVoiceEngine>(uniqueName, friendlyName);
}
} // namespace boo
