#include "AudioVoiceEngine.hpp"
#include "logvisor/logvisor.hpp"
#include "boo/audiodev/IAudioVoiceEngine.hpp"

static logvisor::Module Log("boo::WAVOut");

struct WAVOutVoiceEngine : boo::BaseAudioVoiceEngine
{
    std::vector<float> m_interleavedBuf;

    boo::AudioChannelSet _getAvailableSet()
    {
        return boo::AudioChannelSet::Stereo;
    }

    std::string getCurrentAudioOutput() const
    {
        return "wavout";
    }

    bool setCurrentAudioOutput(const char* name)
    {
        return false;
    }

    std::vector<std::pair<std::string, std::string>> enumerateAudioOutputs() const
    {
        return {{"wavout", "WAVOut"}};
    }

    std::vector<std::pair<std::string, std::string>> enumerateMIDIInputs() const
    {
        return {};
    }

    bool supportsVirtualMIDIIn() const
    {
        return false;
    }

    boo::ReceiveFunctor* m_midiReceiver = nullptr;

    struct MIDIIn : public boo::IMIDIIn
    {
        MIDIIn(WAVOutVoiceEngine* parent, bool virt, boo::ReceiveFunctor&& receiver)
        : IMIDIIn(parent, virt, std::move(receiver)) {}

        std::string description() const
        {
            return "WAVOut MIDI";
        }
    };

    std::unique_ptr<boo::IMIDIIn> newVirtualMIDIIn(boo::ReceiveFunctor&& receiver)
    {
        std::unique_ptr<boo::IMIDIIn> ret = std::make_unique<MIDIIn>(nullptr, true, std::move(receiver));
        m_midiReceiver = &ret->m_receiver;
        return ret;
    }

    std::unique_ptr<boo::IMIDIOut> newVirtualMIDIOut()
    {
        return {};
    }

    std::unique_ptr<boo::IMIDIInOut> newVirtualMIDIInOut(boo::ReceiveFunctor&& receiver)
    {
        return {};
    }

    std::unique_ptr<boo::IMIDIIn> newRealMIDIIn(const char* name, boo::ReceiveFunctor&& receiver)
    {
        return {};
    }

    std::unique_ptr<boo::IMIDIOut> newRealMIDIOut(const char* name)
    {
        return {};
    }

    std::unique_ptr<boo::IMIDIInOut> newRealMIDIInOut(const char* name, boo::ReceiveFunctor&& receiver)
    {
        return {};
    }

    bool useMIDILock() const {return false;}

    FILE* m_fp = nullptr;
    size_t m_bytesWritten = 0;

    void prepareWAV(double sampleRate)
    {
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
        uint16_t chCount = 2;
        fwrite(&chCount, 1, 2, m_fp);
        uint32_t sampRate = sampleRate;
        fwrite(&sampRate, 1, 4, m_fp);
        uint16_t blockAlign = 8;
        uint32_t byteRate = sampRate * blockAlign;
        fwrite(&byteRate, 1, 4, m_fp);
        fwrite(&blockAlign, 1, 2, m_fp);
        uint16_t bps = 32;
        fwrite(&bps, 1, 2, m_fp);

        fwrite("data", 1, 4, m_fp);
        fwrite(&dataSize, 1, 4, m_fp);

        m_mixInfo.m_periodFrames = 512;
        m_mixInfo.m_sampleRate = sampleRate;
        m_mixInfo.m_sampleFormat = SOXR_FLOAT32_I;
        m_mixInfo.m_bitsPerSample = 32;
        _buildAudioRenderClient();
    }

    WAVOutVoiceEngine(const char* path, double sampleRate)
    {
        m_fp = fopen(path, "wb");
        if (!m_fp)
            return;
        prepareWAV(sampleRate);
    }

#if _WIN32
    WAVOutVoiceEngine(const wchar_t* path, double sampleRate)
    {
        m_fp = _wfopen(path, L"wb");
        if (!m_fp)
            return;
        prepareWAV(sampleRate);
    }
#endif

    void finishWav()
    {
        uint32_t dataSize = m_bytesWritten;

        fseek(m_fp, 4, SEEK_SET);
        uint32_t chunkSize = 36 + dataSize;
        fwrite(&chunkSize, 1, 4, m_fp);

        fseek(m_fp, 40, SEEK_SET);
        fwrite(&dataSize, 1, 4, m_fp);

        fclose(m_fp);
    }

    ~WAVOutVoiceEngine()
    {
        finishWav();
    }

    void _buildAudioRenderClient()
    {
        m_mixInfo.m_channels = _getAvailableSet();
        unsigned chCount = ChannelCount(m_mixInfo.m_channels);

        m_5msFrames = m_mixInfo.m_sampleRate * 5 / 1000;
        m_interleavedBuf.resize(2 * m_5msFrames);

        boo::ChannelMap& chMapOut = m_mixInfo.m_channelMap;
        chMapOut.m_channelCount = 2;
        chMapOut.m_channels[0] = boo::AudioChannel::FrontLeft;
        chMapOut.m_channels[1] = boo::AudioChannel::FrontRight;

        while (chMapOut.m_channelCount < chCount)
            chMapOut.m_channels[chMapOut.m_channelCount++] = boo::AudioChannel::Unknown;
    }

    void _rebuildAudioRenderClient(double sampleRate, size_t periodFrames)
    {
        m_mixInfo.m_periodFrames = periodFrames;
        m_mixInfo.m_sampleRate = sampleRate;
        _buildAudioRenderClient();
        _resetSampleRate();
    }

    void pumpAndMixVoices()
    {
        _pumpAndMixVoices(m_5msFrames, m_interleavedBuf.data());
        fwrite(m_interleavedBuf.data(), 1, m_5msFrames * 8, m_fp);
        m_bytesWritten += m_5msFrames * 8;
    }
};

namespace boo
{

std::unique_ptr<IAudioVoiceEngine> NewWAVAudioVoiceEngine(const char* path, double sampleRate)
{
    std::unique_ptr<IAudioVoiceEngine> ret = std::make_unique<WAVOutVoiceEngine>(path, sampleRate);
    if (!static_cast<WAVOutVoiceEngine&>(*ret).m_fp)
        return {};
    return ret;
}

#if _WIN32
std::unique_ptr<IAudioVoiceEngine> NewWAVAudioVoiceEngine(const wchar_t* path, double sampleRate)
{
    std::unique_ptr<IAudioVoiceEngine> ret = std::make_unique<WAVOutVoiceEngine>(path, sampleRate);
    if (!static_cast<WAVOutVoiceEngine&>(*ret).m_fp)
        return {};
    return ret;
}
#endif

}
