#include "../win/Win32Common.hpp"
#include "boo/audiodev/IAudioVoiceAllocator.hpp"
#include <LogVisor/LogVisor.hpp>

#include <xaudio2.h>

namespace boo
{
static LogVisor::LogModule Log("boo::XAudio2");

struct XA2AudioVoice : IAudioVoice
{
    ChannelMap m_map;
    IAudioVoiceCallback* m_cb;
    IXAudio2SourceVoice* m_voiceQueue;
    XAUDIO2_BUFFER m_buffers[3] = {};
    size_t m_bufferFrames = 2048;
    size_t m_frameSize;

    const ChannelMap& channelMap() const {return m_map;}

    unsigned m_fillBuf = 0;
    struct Callback : IXAudio2VoiceCallback
    {
        XA2AudioVoice& m_voice;
        Callback(XA2AudioVoice& voice) : m_voice(voice) {}

        STDMETHOD_(void, OnBufferEnd)(void* pBufferContext)
        {
            m_voice.m_cb->needsNextBuffer(&m_voice, m_voice.m_bufferFrames);
        }
        STDMETHOD_(void, OnBufferStart)(void* pBufferContext) {}
        STDMETHOD_(void, OnLoopEnd)(void* pBufferContext) {}
        STDMETHOD_(void, OnStreamEnd)() {}
        STDMETHOD_(void, OnVoiceError)(void* pBufferContext, HRESULT error) {}
        STDMETHOD_(void, OnVoiceProcessingPassEnd)() {}
        STDMETHOD_(void, OnVoiceProcessingPassStart)(UINT32 bytes_required) {}
    } m_xaCb;

    XA2AudioVoice(IXAudio2& xa2, AudioChannelSet set, unsigned sampleRate, IAudioVoiceCallback* cb)
    : m_cb(cb), m_xaCb(*this)
    {
        unsigned chCount = ChannelCount(set);

        WAVEFORMATEX desc = {};
        desc.wFormatTag = WAVE_FORMAT_PCM;
        desc.nChannels = chCount;
        desc.nSamplesPerSec = sampleRate;
        desc.wBitsPerSample = 16;
        desc.nBlockAlign = desc.nChannels * desc.wBitsPerSample / 8;
        desc.nAvgBytesPerSec = desc.nAvgBytesPerSec * desc.nBlockAlign;

        if FAILED(xa2.CreateSourceVoice(&m_voiceQueue, &desc, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &m_xaCb))
        {
            Log.report(LogVisor::Error, "unable to create source voice");
            return;
        }

        XAUDIO2_VOICE_DETAILS voxDetails;
        m_voiceQueue->GetVoiceDetails(&voxDetails);
        switch (voxDetails.InputChannels)
        {
        case 2:
            m_map.m_channelCount = 2;
            m_map.m_channels[0] = AudioChannel::FrontLeft;
            m_map.m_channels[1] = AudioChannel::FrontRight;
            break;
        case 4:
            m_map.m_channelCount = 4;
            m_map.m_channels[0] = AudioChannel::FrontLeft;
            m_map.m_channels[1] = AudioChannel::FrontRight;
            m_map.m_channels[2] = AudioChannel::RearLeft;
            m_map.m_channels[3] = AudioChannel::RearRight;
            break;
        case 5:
            m_map.m_channelCount = 5;
            m_map.m_channels[0] = AudioChannel::FrontLeft;
            m_map.m_channels[1] = AudioChannel::FrontRight;
            m_map.m_channels[2] = AudioChannel::FrontCenter;
            m_map.m_channels[3] = AudioChannel::RearLeft;
            m_map.m_channels[4] = AudioChannel::RearRight;
            break;
        case 6:
            m_map.m_channelCount = 6;
            m_map.m_channels[0] = AudioChannel::FrontLeft;
            m_map.m_channels[1] = AudioChannel::FrontRight;
            m_map.m_channels[2] = AudioChannel::FrontCenter;
            m_map.m_channels[3] = AudioChannel::LFE;
            m_map.m_channels[4] = AudioChannel::RearLeft;
            m_map.m_channels[5] = AudioChannel::RearRight;
            break;
        case 8:
            m_map.m_channelCount = 8;
            m_map.m_channels[0] = AudioChannel::FrontLeft;
            m_map.m_channels[1] = AudioChannel::FrontRight;
            m_map.m_channels[2] = AudioChannel::FrontCenter;
            m_map.m_channels[3] = AudioChannel::LFE;
            m_map.m_channels[4] = AudioChannel::RearLeft;
            m_map.m_channels[5] = AudioChannel::RearRight;
            m_map.m_channels[6] = AudioChannel::SideLeft;
            m_map.m_channels[7] = AudioChannel::SideRight;
            break;
        default:
            Log.report(LogVisor::Error, "unknown channel layout %u; using stereo", voxDetails.InputChannels);
            m_map.m_channelCount = 2;
            m_map.m_channels[0] = AudioChannel::FrontLeft;
            m_map.m_channels[1] = AudioChannel::FrontRight;
            break;
        }

        while (m_map.m_channelCount < chCount)
            m_map.m_channels[m_map.m_channelCount++] = AudioChannel::Unknown;

        m_frameSize = chCount * 2;

        for (unsigned i=0 ; i<3 ; ++i)
            m_cb->needsNextBuffer(this, m_bufferFrames);
    }

    void bufferSampleData(const int16_t* data, size_t frames)
    {
        XAUDIO2_BUFFER* buf = &m_buffers[m_fillBuf++];
        if (m_fillBuf == 3)
            m_fillBuf = 0;
        buf->AudioBytes = frames * m_frameSize;
        buf->pAudioData = reinterpret_cast<const BYTE*>(data);
        m_voiceQueue->SubmitSourceBuffer(buf);
    }

    ~XA2AudioVoice()
    {
        m_voiceQueue->DestroyVoice();
    }

    void start()
    {
        m_voiceQueue->Start();
    }

    void stop()
    {
        m_voiceQueue->Stop();
    }
};

struct XA2AudioVoiceAllocator : IAudioVoiceAllocator
{
    ComPtr<IXAudio2> m_xa2;
    IXAudio2MasteringVoice* m_masterVoice;
    AudioChannelSet m_maxSet;

    XA2AudioVoiceAllocator()
    {
        if (FAILED(XAudio2Create(&m_xa2)))
        {
            Log.report(LogVisor::Error, "Unable to initialize XAudio2");
            return;
        }
        if (FAILED(m_xa2->CreateMasteringVoice(&m_masterVoice)))
        {
            Log.report(LogVisor::Error, "Unable to initialize XAudio2 mastering voice");
            return;
        }
        DWORD channelMask;
        if (FAILED(m_masterVoice->GetChannelMask(&channelMask)))
        {
            Log.report(LogVisor::Error, "Unable to get mastering voice's channel mask");
            return;
        }
        if ((channelMask & (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT)) == (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT))
        {
            m_maxSet = AudioChannelSet::Stereo;
            if ((channelMask & (SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT)) == (SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT))
            {
                m_maxSet = AudioChannelSet::Quad;
                if ((channelMask & (SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY)) == (SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY))
                {
                    m_maxSet = AudioChannelSet::Surround51;
                    if ((channelMask & (SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)) == (SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT))
                    {
                        m_maxSet = AudioChannelSet::Surround71;
                    }
                }
            }
        }
    }

    ~XA2AudioVoiceAllocator()
    {
        m_masterVoice->DestroyVoice();
    }

    std::unique_ptr<IAudioVoice> allocateNewVoice(AudioChannelSet layoutOut,
                                                  unsigned sampleRate,
                                                  IAudioVoiceCallback* cb)
    {
        AudioChannelSet acset = std::min(layoutOut, m_maxSet);
        XA2AudioVoice* newVoice = new XA2AudioVoice(*m_xa2.Get(), acset, sampleRate, cb);
        std::unique_ptr<IAudioVoice> ret(newVoice);
        if (!newVoice->m_voiceQueue)
            return {};
        return ret;
    }
};

}
