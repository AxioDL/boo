#include "../win/Win32Common.hpp"
#include "AudioVoiceEngine.hpp"
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

struct WASAPIAudioVoiceEngine : BaseAudioVoiceEngine
{
    ComPtr<IMMDevice> m_device;
    ComPtr<IAudioClient> m_audClient;
    ComPtr<IAudioRenderClient> m_renderClient;

    WASAPIAudioVoiceEngine()
    {
        /* Enumerate default audio device */
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
            m_device.Reset();
            return;
        }

        if (FAILED(m_device->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, &m_audClient)))
        {
            Log.report(logvisor::Fatal, L"unable to create audio client from device");
            m_device.Reset();
            return;
        }

        WAVEFORMATEXTENSIBLE* pwfx;
        if (FAILED(m_audClient->GetMixFormat((WAVEFORMATEX**)&pwfx)))
        {
            Log.report(logvisor::Fatal, L"unable to obtain audio mix format from device");
            m_device.Reset();
            return;
        }

        /* Get channel information */
        if ((pwfx->dwChannelMask & (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT)) == (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT))
        {
            m_mixInfo.m_channels = AudioChannelSet::Stereo;
            if ((pwfx->dwChannelMask & (SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT)) == (SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT))
            {
                m_mixInfo.m_channels = AudioChannelSet::Quad;
                if ((pwfx->dwChannelMask & (SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY)) == (SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY))
                {
                    m_mixInfo.m_channels = AudioChannelSet::Surround51;
                    if ((pwfx->dwChannelMask & (SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)) == (SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT))
                    {
                        m_mixInfo.m_channels = AudioChannelSet::Surround71;
                    }
                }
            }
        }

        ChannelMap& chMapOut = m_mixInfo.m_channelMap;
        switch (pwfx->Format.nChannels)
        {
        case 2:
            chMapOut.m_channelCount = 2;
            chMapOut.m_channels[0] = AudioChannel::FrontLeft;
            chMapOut.m_channels[1] = AudioChannel::FrontRight;
            break;
        case 4:
            chMapOut.m_channelCount = 4;
            chMapOut.m_channels[0] = AudioChannel::FrontLeft;
            chMapOut.m_channels[1] = AudioChannel::FrontRight;
            chMapOut.m_channels[2] = AudioChannel::RearLeft;
            chMapOut.m_channels[3] = AudioChannel::RearRight;
            break;
        case 5:
            chMapOut.m_channelCount = 5;
            chMapOut.m_channels[0] = AudioChannel::FrontLeft;
            chMapOut.m_channels[1] = AudioChannel::FrontRight;
            chMapOut.m_channels[2] = AudioChannel::FrontCenter;
            chMapOut.m_channels[3] = AudioChannel::RearLeft;
            chMapOut.m_channels[4] = AudioChannel::RearRight;
            break;
        case 6:
            chMapOut.m_channelCount = 6;
            chMapOut.m_channels[0] = AudioChannel::FrontLeft;
            chMapOut.m_channels[1] = AudioChannel::FrontRight;
            chMapOut.m_channels[2] = AudioChannel::FrontCenter;
            chMapOut.m_channels[3] = AudioChannel::LFE;
            chMapOut.m_channels[4] = AudioChannel::RearLeft;
            chMapOut.m_channels[5] = AudioChannel::RearRight;
            break;
        case 8:
            chMapOut.m_channelCount = 8;
            chMapOut.m_channels[0] = AudioChannel::FrontLeft;
            chMapOut.m_channels[1] = AudioChannel::FrontRight;
            chMapOut.m_channels[2] = AudioChannel::FrontCenter;
            chMapOut.m_channels[3] = AudioChannel::LFE;
            chMapOut.m_channels[4] = AudioChannel::RearLeft;
            chMapOut.m_channels[5] = AudioChannel::RearRight;
            chMapOut.m_channels[6] = AudioChannel::SideLeft;
            chMapOut.m_channels[7] = AudioChannel::SideRight;
            break;
        default:
            Log.report(logvisor::Warning, "unknown channel layout %u; using stereo", pwfx->Format.nChannels);
            chMapOut.m_channelCount = 2;
            chMapOut.m_channels[0] = AudioChannel::FrontLeft;
            chMapOut.m_channels[1] = AudioChannel::FrontRight;
            break;
        }

        /* Initialize audio client */
        if (FAILED(m_audClient->Initialize(
                   AUDCLNT_SHAREMODE_SHARED,
                   0,
                   1000000,
                   0,
                   (WAVEFORMATEX*)pwfx,
                   nullptr)))
        {
            Log.report(logvisor::Fatal, L"unable to initialize audio client");
            m_device.Reset();
            CoTaskMemFree(pwfx);
            return;
        }
        m_mixInfo.m_sampleRate = pwfx->Format.nSamplesPerSec;

        if (pwfx->Format.wFormatTag == WAVE_FORMAT_PCM ||
            (pwfx->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE && pwfx->SubFormat == KSDATAFORMAT_SUBTYPE_PCM))
        {
            if (pwfx->Format.wBitsPerSample == 16)
            {
                m_mixInfo.m_sampleFormat = SOXR_INT16_I;
                m_mixInfo.m_bitsPerSample = 16;
            }
            else if (pwfx->Format.wBitsPerSample == 32)
            {
                m_mixInfo.m_sampleFormat = SOXR_INT32_I;
                m_mixInfo.m_bitsPerSample = 32;
            }
            else
            {
                Log.report(logvisor::Fatal, L"unsupported bits-per-sample %d", pwfx->Format.wBitsPerSample);
                m_device.Reset();
                return;
            }
        }
        else if (pwfx->Format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                 (pwfx->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE && pwfx->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
        {
            if (pwfx->Format.wBitsPerSample == 32)
            {
                m_mixInfo.m_sampleFormat = SOXR_FLOAT32_I;
                m_mixInfo.m_bitsPerSample = 32;
            }
            else
            {
                Log.report(logvisor::Fatal, L"unsupported floating-point bits-per-sample %d", pwfx->Format.wBitsPerSample);
                m_device.Reset();
                return;
            }
        }

        CoTaskMemFree(pwfx);

        UINT32 bufferFrameCount;
        if (FAILED(m_audClient->GetBufferSize(&bufferFrameCount)))
        {
            Log.report(logvisor::Fatal, L"unable to get audio buffer frame count");
            m_device.Reset();
            return;
        }
        m_mixInfo.m_periodFrames = bufferFrameCount;

        if (FAILED(m_audClient->GetService(IID_IAudioRenderClient, &m_renderClient)))
        {
            Log.report(logvisor::Fatal, L"unable to create audio render client");
            m_device.Reset();
            return;
        }
    }

    bool m_started = false;

    void pumpAndMixVoices()
    {
        UINT32 numFramesPadding;
        if (FAILED(m_audClient->GetCurrentPadding(&numFramesPadding)))
        {
            Log.report(logvisor::Fatal, L"unable to get available buffer frames");
            return;
        }

        size_t frames = m_mixInfo.m_periodFrames - numFramesPadding;
        if (frames <= 0)
            return;

        BYTE* bufOut;
        if (FAILED(m_renderClient->GetBuffer(frames, &bufOut)))
        {
            Log.report(logvisor::Fatal, L"unable to map audio buffer");
            return;
        }

        DWORD flags = 0;
        switch (m_mixInfo.m_sampleFormat)
        {
        case SOXR_INT16_I:
            _pumpAndMixVoices(frames, reinterpret_cast<int16_t*>(bufOut));
            break;
        case SOXR_INT32_I:
            _pumpAndMixVoices(frames, reinterpret_cast<int32_t*>(bufOut));
            break;
        case SOXR_FLOAT32_I:
            _pumpAndMixVoices(frames, reinterpret_cast<float*>(bufOut));
            break;
        default:
            flags = AUDCLNT_BUFFERFLAGS_SILENT;
            break;
        }

        if (FAILED(m_renderClient->ReleaseBuffer(frames, flags)))
        {
            Log.report(logvisor::Fatal, L"unable to unmap audio buffer");
            return;
        }

        if (!m_started)
        {
            if (FAILED(m_audClient->Start()))
            {
                Log.report(logvisor::Fatal, L"unable to start audio client");
                m_device.Reset();
                return;
            }
            m_started = true;
        }
    }
};

std::unique_ptr<IAudioVoiceEngine> NewAudioVoiceEngine()
{
    std::unique_ptr<IAudioVoiceEngine> ret = std::make_unique<WASAPIAudioVoiceEngine>();
    if (!static_cast<WASAPIAudioVoiceEngine&>(*ret).m_device)
        return {};
    return ret;
}

}
