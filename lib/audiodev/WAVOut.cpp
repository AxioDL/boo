#include "AudioVoiceEngine.hpp"
#include "logvisor/logvisor.hpp"
#include "boo/audiodev/IAudioVoiceEngine.hpp"

namespace boo {

static logvisor::Module Log("boo::WAVOut");

struct WAVOutVoiceEngine : BaseAudioVoiceEngine {
  std::vector<float> m_interleavedBuf;

  AudioChannelSet _getAvailableSet() { return AudioChannelSet::Stereo; }

  std::string getCurrentAudioOutput() const override { return "wavout"; }

  bool setCurrentAudioOutput(const char* name) override { return false; }

  std::vector<std::pair<std::string, std::string>> enumerateAudioOutputs() const override {
    return {{"wavout", "WAVOut"}};
  }

  std::vector<std::pair<std::string, std::string>> enumerateMIDIInputs() const override { return {}; }

  bool supportsVirtualMIDIIn() const override { return false; }

  ReceiveFunctor* m_midiReceiver = nullptr;

  struct MIDIIn : public IMIDIIn {
    MIDIIn(WAVOutVoiceEngine* parent, bool virt, ReceiveFunctor&& receiver)
    : IMIDIIn(parent, virt, std::move(receiver)) {}

    std::string description() const override { return "WAVOut MIDI"; }
  };

  std::unique_ptr<IMIDIIn> newVirtualMIDIIn(ReceiveFunctor&& receiver) override {
    std::unique_ptr<IMIDIIn> ret = std::make_unique<MIDIIn>(nullptr, true, std::move(receiver));
    m_midiReceiver = &ret->m_receiver;
    return ret;
  }

  std::unique_ptr<IMIDIOut> newVirtualMIDIOut() override { return {}; }

  std::unique_ptr<IMIDIInOut> newVirtualMIDIInOut(ReceiveFunctor&& receiver) override { return {}; }

  std::unique_ptr<IMIDIIn> newRealMIDIIn(const char* name, ReceiveFunctor&& receiver) override { return {}; }

  std::unique_ptr<IMIDIOut> newRealMIDIOut(const char* name) override { return {}; }

  std::unique_ptr<IMIDIInOut> newRealMIDIInOut(const char* name, ReceiveFunctor&& receiver) override { return {}; }

  bool useMIDILock() const override { return false; }

  FILE* m_fp = nullptr;
  size_t m_bytesWritten = 0;

  void prepareWAV(double sampleRate, int numChans) {
    uint32_t speakerMask = 0;

    switch (numChans) {
    default:
    case 2:
      numChans = 2;
      m_mixInfo.m_channels = AudioChannelSet::Stereo;
      m_mixInfo.m_channelMap.m_channelCount = 2;
      m_mixInfo.m_channelMap.m_channels[0] = AudioChannel::FrontLeft;
      m_mixInfo.m_channelMap.m_channels[1] = AudioChannel::FrontRight;
      speakerMask = 0x00000001 | 0x00000002;
      break;
    case 4:
      numChans = 4;
      m_mixInfo.m_channels = AudioChannelSet::Quad;
      m_mixInfo.m_channelMap.m_channelCount = 4;
      m_mixInfo.m_channelMap.m_channels[0] = AudioChannel::FrontLeft;
      m_mixInfo.m_channelMap.m_channels[1] = AudioChannel::FrontRight;
      m_mixInfo.m_channelMap.m_channels[2] = AudioChannel::RearLeft;
      m_mixInfo.m_channelMap.m_channels[3] = AudioChannel::RearRight;
      speakerMask = 0x00000001 | 0x00000002 | 0x00000010 | 0x00000020;
      break;
    case 6:
      numChans = 6;
      m_mixInfo.m_channels = AudioChannelSet::Surround51;
      m_mixInfo.m_channelMap.m_channelCount = 6;
      m_mixInfo.m_channelMap.m_channels[0] = AudioChannel::FrontLeft;
      m_mixInfo.m_channelMap.m_channels[1] = AudioChannel::FrontRight;
      m_mixInfo.m_channelMap.m_channels[2] = AudioChannel::FrontCenter;
      m_mixInfo.m_channelMap.m_channels[3] = AudioChannel::LFE;
      m_mixInfo.m_channelMap.m_channels[4] = AudioChannel::RearLeft;
      m_mixInfo.m_channelMap.m_channels[5] = AudioChannel::RearRight;
      speakerMask = 0x00000001 | 0x00000002 | 0x00000004 | 0x00000008 | 0x00000010 | 0x00000020;
      break;
    case 8:
      numChans = 8;
      m_mixInfo.m_channels = AudioChannelSet::Surround71;
      m_mixInfo.m_channelMap.m_channelCount = 8;
      m_mixInfo.m_channelMap.m_channels[0] = AudioChannel::FrontLeft;
      m_mixInfo.m_channelMap.m_channels[1] = AudioChannel::FrontRight;
      m_mixInfo.m_channelMap.m_channels[2] = AudioChannel::FrontCenter;
      m_mixInfo.m_channelMap.m_channels[3] = AudioChannel::LFE;
      m_mixInfo.m_channelMap.m_channels[4] = AudioChannel::RearLeft;
      m_mixInfo.m_channelMap.m_channels[5] = AudioChannel::RearRight;
      m_mixInfo.m_channelMap.m_channels[6] = AudioChannel::SideLeft;
      m_mixInfo.m_channelMap.m_channels[7] = AudioChannel::SideRight;
      speakerMask =
          0x00000001 | 0x00000002 | 0x00000004 | 0x00000008 | 0x00000010 | 0x00000020 | 0x00000200 | 0x00000400;
      break;
    }

    if (numChans == 2) {
      fwrite("RIFF", 1, 4, m_fp);
      uint32_t dataSize = 0;
      uint32_t chunkSize = 36 + dataSize;
      fwrite(&chunkSize, 1, 4, m_fp);

      fwrite("WAVE", 1, 4, m_fp);

      fwrite("fmt ", 1, 4, m_fp);
      uint32_t sixteen = 16;
      fwrite(&sixteen, 1, 4, m_fp);
      uint16_t audioFmt = 3;
      fwrite(&audioFmt, 1, 2, m_fp);
      uint16_t chCount = numChans;
      fwrite(&chCount, 1, 2, m_fp);
      uint32_t sampRate = sampleRate;
      fwrite(&sampRate, 1, 4, m_fp);
      uint16_t blockAlign = 4 * numChans;
      uint32_t byteRate = sampRate * blockAlign;
      fwrite(&byteRate, 1, 4, m_fp);
      fwrite(&blockAlign, 1, 2, m_fp);
      uint16_t bps = 32;
      fwrite(&bps, 1, 2, m_fp);

      fwrite("data", 1, 4, m_fp);
      fwrite(&dataSize, 1, 4, m_fp);
    } else {
      fwrite("RIFF", 1, 4, m_fp);
      uint32_t dataSize = 0;
      uint32_t chunkSize = 60 + dataSize;
      fwrite(&chunkSize, 1, 4, m_fp);

      fwrite("WAVE", 1, 4, m_fp);

      fwrite("fmt ", 1, 4, m_fp);
      uint32_t forty = 40;
      fwrite(&forty, 1, 4, m_fp);
      uint16_t audioFmt = 0xFFFE;
      fwrite(&audioFmt, 1, 2, m_fp);
      uint16_t chCount = numChans;
      fwrite(&chCount, 1, 2, m_fp);
      uint32_t sampRate = sampleRate;
      fwrite(&sampRate, 1, 4, m_fp);
      uint16_t blockAlign = 4 * numChans;
      uint32_t byteRate = sampRate * blockAlign;
      fwrite(&byteRate, 1, 4, m_fp);
      fwrite(&blockAlign, 1, 2, m_fp);
      uint16_t bps = 32;
      fwrite(&bps, 1, 2, m_fp);
      uint16_t extSize = 22;
      fwrite(&extSize, 1, 2, m_fp);
      fwrite(&bps, 1, 2, m_fp);
      fwrite(&speakerMask, 1, 4, m_fp);
      fwrite("\x03\x00\x00\x00\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71", 1, 16, m_fp);

      fwrite("data", 1, 4, m_fp);
      fwrite(&dataSize, 1, 4, m_fp);
    }

    m_mixInfo.m_periodFrames = 512;
    m_mixInfo.m_sampleRate = sampleRate;
    m_mixInfo.m_sampleFormat = SOXR_FLOAT32_I;
    m_mixInfo.m_bitsPerSample = 32;
    _buildAudioRenderClient();
  }

  WAVOutVoiceEngine(const char* path, double sampleRate, int numChans) {
    m_fp = fopen(path, "wb");
    if (!m_fp)
      return;
    prepareWAV(sampleRate, numChans);
  }

#if _WIN32
  WAVOutVoiceEngine(const wchar_t* path, double sampleRate, int numChans) {
    m_fp = _wfopen(path, L"wb");
    if (!m_fp)
      return;
    prepareWAV(sampleRate, numChans);
  }
#endif

  void finishWav() {
    uint32_t dataSize = m_bytesWritten;

    if (m_mixInfo.m_channelMap.m_channelCount == 2) {
      fseek(m_fp, 4, SEEK_SET);
      uint32_t chunkSize = 36 + dataSize;
      fwrite(&chunkSize, 1, 4, m_fp);

      fseek(m_fp, 40, SEEK_SET);
      fwrite(&dataSize, 1, 4, m_fp);
    } else {
      fseek(m_fp, 4, SEEK_SET);
      uint32_t chunkSize = 60 + dataSize;
      fwrite(&chunkSize, 1, 4, m_fp);

      fseek(m_fp, 64, SEEK_SET);
      fwrite(&dataSize, 1, 4, m_fp);
    }

    fclose(m_fp);
  }

  ~WAVOutVoiceEngine() override { finishWav(); }

  void _buildAudioRenderClient() {
    m_5msFrames = m_mixInfo.m_sampleRate * 5 / 1000;
    m_interleavedBuf.resize(m_mixInfo.m_channelMap.m_channelCount * m_5msFrames);
  }

  void _rebuildAudioRenderClient(double sampleRate, size_t periodFrames) {
    m_mixInfo.m_periodFrames = periodFrames;
    m_mixInfo.m_sampleRate = sampleRate;
    _buildAudioRenderClient();
    _resetSampleRate();
  }

  void pumpAndMixVoices() override {
    size_t frameSz = 4 * m_mixInfo.m_channelMap.m_channelCount;
    _pumpAndMixVoices(m_5msFrames, m_interleavedBuf.data());
    fwrite(m_interleavedBuf.data(), 1, m_5msFrames * frameSz, m_fp);
    m_bytesWritten += m_5msFrames * frameSz;
  }
};

std::unique_ptr<IAudioVoiceEngine> NewWAVAudioVoiceEngine(const char* path, double sampleRate, int numChans) {
  std::unique_ptr<IAudioVoiceEngine> ret = std::make_unique<WAVOutVoiceEngine>(path, sampleRate, numChans);
  if (!static_cast<WAVOutVoiceEngine&>(*ret).m_fp)
    return {};
  return ret;
}

#if _WIN32
std::unique_ptr<IAudioVoiceEngine> NewWAVAudioVoiceEngine(const wchar_t* path, double sampleRate, int numChans) {
  std::unique_ptr<IAudioVoiceEngine> ret = std::make_unique<WAVOutVoiceEngine>(path, sampleRate, numChans);
  if (!static_cast<WAVOutVoiceEngine&>(*ret).m_fp)
    return {};
  return ret;
}
#endif

} // namespace boo
