#include "../win/Win32Common.hpp"
#include "boo/audiodev/IAudioVoiceAllocator.hpp"
#include "logvisor/logvisor.hpp"

#include <Mmdeviceapi.h>
#include <Audioclient.h>

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

namespace boo
{
static logvisor::Module Log("boo::WASAPI");

struct WASAPIAudioVoice : IAudioVoice
{
    struct WASAPIAudioVoiceAllocator& m_parent;
    std::list<WASAPIAudioVoice*>::iterator m_parentIt;

    ChannelMap m_map;
    IAudioVoiceCallback* m_cb;
    ComPtr<IAudioClient> m_audClient;
    ComPtr<IAudioRenderClient> m_renderClient;
    UINT32  m_bufferFrames = 1024;
    size_t m_frameSize;

    const ChannelMap& channelMap() const {return m_map;}

    WASAPIAudioVoice(WASAPIAudioVoiceAllocator& parent, IMMDevice* dev, AudioChannelSet set,
                     unsigned sampleRate, IAudioVoiceCallback* cb)
    : m_parent(parent), m_cb(cb)
    {
        unsigned chCount = ChannelCount(set);

        WAVEFORMATEX desc = {};
        desc.wFormatTag = WAVE_FORMAT_PCM;
        desc.nChannels = chCount;
        desc.nSamplesPerSec = sampleRate;
        desc.wBitsPerSample = 16;
        desc.nBlockAlign = desc.nChannels * desc.wBitsPerSample / 8;
        desc.nAvgBytesPerSec = desc.nSamplesPerSec * desc.nBlockAlign;

        if (FAILED(dev->Activate(IID_IAudioClient, CLSCTX_ALL,
                                 nullptr, &m_audClient)))
        {
            Log.report(logvisor::Fatal, "unable to create audio client");
            return;
        }

        WAVEFORMATEX* works;
        m_audClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &desc, &works);

        HRESULT hr = m_audClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
                                             1000000, 0, &desc, nullptr);

        if (FAILED(hr))
        {
            Log.report(logvisor::Fatal, "unable to initialize audio client");
            return;
        }

        if (FAILED(m_audClient->GetBufferSize(&m_bufferFrames)))
        {
            Log.report(logvisor::Fatal, "unable to obtain audio buffer size");
            return;
        }

        if (FAILED(m_audClient->GetService(IID_IAudioRenderClient, &m_renderClient)))
        {
            Log.report(logvisor::Fatal, "unable to create audio render client");
            return;
        }

        switch (chCount)
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
            Log.report(logvisor::Error, "unknown channel layout %u; using stereo", chCount);
            m_map.m_channelCount = 2;
            m_map.m_channels[0] = AudioChannel::FrontLeft;
            m_map.m_channels[1] = AudioChannel::FrontRight;
            break;
        }

        while (m_map.m_channelCount < chCount)
            m_map.m_channels[m_map.m_channelCount++] = AudioChannel::Unknown;

        m_frameSize = chCount * 2;

        for (unsigned i=0 ; i<3 ; ++i)
            m_cb->needsNextBuffer(*this, m_bufferFrames);
    }

    void bufferSampleData(const int16_t* data, size_t frames)
    {
        BYTE* dataOut;
        if (FAILED(m_renderClient->GetBuffer(frames, &dataOut)))
        {
            Log.report(logvisor::Fatal, L"unable to obtain audio buffer");
            return;
        }

        memcpy(dataOut, data, frames * m_frameSize);

        if (FAILED(m_renderClient->ReleaseBuffer(frames, 0)))
        {
            Log.report(logvisor::Fatal, L"unable to release audio buffer");
            return;
        }
    }

    void start()
    {
        m_audClient->Start();
    }

    void stop()
    {
        m_audClient->Stop();
    }

    void pump()
    {
        UINT32 padding;
        if (FAILED(m_audClient->GetCurrentPadding(&padding)))
        {
            Log.report(logvisor::Fatal, L"unable to obtain audio buffer padding");
            return;
        }
        INT32 available = m_bufferFrames - padding;
        m_cb->needsNextBuffer(*this, available);
    }

    ~WASAPIAudioVoice();
};

struct WASAPIAudioVoiceAllocator : IAudioVoiceAllocator
{
    ComPtr<IMMDevice> m_device;
    AudioChannelSet m_maxSet = AudioChannelSet::Unknown;
    std::list<WASAPIAudioVoice*> m_allocatedVoices;

    WASAPIAudioVoiceAllocator()
    {
        ComPtr<IMMDeviceEnumerator> pEnumerator;

        if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr,
                                    CLSCTX_ALL, IID_IMMDeviceEnumerator,
                                    &pEnumerator)))
        {
            Log.report(logvisor::Fatal, L"unable to create MMDeviceEnumerator instance");
            return;
        }

        if (FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_device)))
        {
            Log.report(logvisor::Fatal, L"unable to obtain default audio device");
            return;
        }

        ComPtr<IAudioClient> pAudioClient;
        if (FAILED(m_device->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, &pAudioClient)))
        {
            Log.report(logvisor::Fatal, L"unable to create audio client from device");
            return;
        }

        WAVEFORMATEXTENSIBLE* pwfx;
        if (FAILED(pAudioClient->GetMixFormat((WAVEFORMATEX**)&pwfx)))
        {
            Log.report(logvisor::Fatal, L"unable to obtain audio mix format from device");
            return;
        }

        if ((pwfx->dwChannelMask & (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT)) == (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT))
        {
            m_maxSet = AudioChannelSet::Stereo;
            if ((pwfx->dwChannelMask & (SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT)) == (SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT))
            {
                m_maxSet = AudioChannelSet::Quad;
                if ((pwfx->dwChannelMask & (SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY)) == (SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY))
                {
                    m_maxSet = AudioChannelSet::Surround51;
                    if ((pwfx->dwChannelMask & (SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)) == (SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT))
                    {
                        m_maxSet = AudioChannelSet::Surround71;
                    }
                }
            }
        }

        CoTaskMemFree(pwfx);
    }

    ~WASAPIAudioVoiceAllocator()
    {
    }

    AudioChannelSet getAvailableSet()
    {
        return m_maxSet;
    }

    std::unique_ptr<IAudioVoice> allocateNewVoice(AudioChannelSet layoutOut,
                                                  unsigned sampleRate,
                                                  IAudioVoiceCallback* cb)
    {
        WASAPIAudioVoice* newVoice = new WASAPIAudioVoice(*this, m_device.Get(), layoutOut, sampleRate, cb);
        newVoice->m_parentIt = m_allocatedVoices.insert(m_allocatedVoices.end(), newVoice);
        std::unique_ptr<IAudioVoice> ret(newVoice);
        if (!newVoice->m_audClient)
            return {};
        return ret;
    }

    void pumpVoices()
    {
        for (WASAPIAudioVoice* vox : m_allocatedVoices)
            vox->pump();
    }
};

WASAPIAudioVoice::~WASAPIAudioVoice()
{
    m_parent.m_allocatedVoices.erase(m_parentIt);
}

std::unique_ptr<IAudioVoiceAllocator> NewAudioVoiceAllocator()
{
    return std::make_unique<WASAPIAudioVoiceAllocator>();
}

}
