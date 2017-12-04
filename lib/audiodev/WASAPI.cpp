#include "../win/Win32Common.hpp"
#include "AudioVoiceEngine.hpp"
#include "logvisor/logvisor.hpp"

#include <Mmdeviceapi.h>
#include <Audioclient.h>
#include <mmsystem.h>

#include <iterator>

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

namespace boo
{
static logvisor::Module Log("boo::WASAPI");

#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

struct WASAPIAudioVoiceEngine : BaseAudioVoiceEngine
{
    ComPtr<IMMDeviceEnumerator> m_enumerator;
    ComPtr<IMMDevice> m_device;
    ComPtr<IAudioClient> m_audClient;
    ComPtr<IAudioRenderClient> m_renderClient;

    size_t m_curBufFrame = 0;
    std::vector<float> m_5msBuffer;

    struct NotificationClient : public IMMNotificationClient
    {
        WASAPIAudioVoiceEngine& m_parent;

        LONG _cRef;
        IMMDeviceEnumerator *_pEnumerator;

        NotificationClient(WASAPIAudioVoiceEngine& parent)
        : m_parent(parent),
          _cRef(1),
          _pEnumerator(nullptr)
        {}

        ~NotificationClient()
        {
            SAFE_RELEASE(_pEnumerator)
        }


        // IUnknown methods -- AddRef, Release, and QueryInterface

        ULONG STDMETHODCALLTYPE AddRef()
        {
            return InterlockedIncrement(&_cRef);
        }

        ULONG STDMETHODCALLTYPE Release()
        {
            ULONG ulRef = InterlockedDecrement(&_cRef);
            if (0 == ulRef)
            {
                delete this;
            }
            return ulRef;
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(
                                    REFIID riid, VOID **ppvInterface)
        {
            if (IID_IUnknown == riid)
            {
                AddRef();
                *ppvInterface = (IUnknown*)this;
            }
            else if (__uuidof(IMMNotificationClient) == riid)
            {
                AddRef();
                *ppvInterface = (IMMNotificationClient*)this;
            }
            else
            {
                *ppvInterface = NULL;
                return E_NOINTERFACE;
            }
            return S_OK;
        }

        // Callback methods for device-event notifications.

        HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(
                                    EDataFlow flow, ERole role,
                                    LPCWSTR pwstrDeviceId)
        {
            m_parent.m_rebuild = true;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId)
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId)
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(
                                    LPCWSTR pwstrDeviceId,
                                    DWORD dwNewState)
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(
                                    LPCWSTR pwstrDeviceId,
                                    const PROPERTYKEY key)
        {
            return S_OK;
        }
    } m_notificationClient;

    void _buildAudioRenderClient()
    {
        if (FAILED(m_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_device)))
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
                   450000, /* 45ms */
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
        m_5msFrames = (m_mixInfo.m_sampleRate * 5 / 500 + 1) / 2;
        m_curBufFrame = m_5msFrames;
        m_5msBuffer.resize(m_5msFrames * chMapOut.m_channelCount);

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

    WASAPIAudioVoiceEngine()
    : m_notificationClient(*this)
    {
        /* Enumerate default audio device */
        if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr,
                                    CLSCTX_ALL, IID_IMMDeviceEnumerator,
                                    &m_enumerator)))
        {
            Log.report(logvisor::Fatal, L"unable to create MMDeviceEnumerator instance");
            return;
        }

        if (FAILED(m_enumerator->RegisterEndpointNotificationCallback(&m_notificationClient)))
        {
            Log.report(logvisor::Fatal, L"unable to register multimedia event callback");
            m_device.Reset();
            return;
        }

        _buildAudioRenderClient();
    }

    bool m_started = false;
    bool m_rebuild = false;

    void _rebuildAudioRenderClient()
    {
         soxr_datatype_t oldFmt = m_mixInfo.m_sampleFormat;

         _buildAudioRenderClient();
         m_rebuild = false;
         m_started = false;

         if (m_mixInfo.m_sampleFormat != oldFmt)
             Log.report(logvisor::Fatal, L"audio device sample format changed, boo doesn't support this!!");

         if (m_voiceHead)
             for (AudioVoice& vox : *m_voiceHead)
                 vox._resetSampleRate(vox.m_sampleRateIn);
         if (m_submixHead)
             for (AudioSubmix& smx : *m_submixHead)
                 smx._resetOutputSampleRate();
    }

    void pumpAndMixVoices()
    {
        int attempt = 0;
        while (true)
        {
            if (attempt >= 10)
                Log.report(logvisor::Fatal, L"unable to setup AudioRenderClient");

            if (m_rebuild)
                _rebuildAudioRenderClient();

            HRESULT res;
            if (!m_started)
            {
                res = m_audClient->Start();
                if (FAILED(res))
                {
                    m_rebuild = true;
                    ++attempt;
                    continue;
                }
                m_started = true;
            }

            UINT32 numFramesPadding;
            res = m_audClient->GetCurrentPadding(&numFramesPadding);
            if (FAILED(res))
            {
                m_rebuild = true;
                ++attempt;
                continue;
            }

            size_t frames = m_mixInfo.m_periodFrames - numFramesPadding;
            if (frames <= 0)
                return;

            BYTE* bufOut;
            res = m_renderClient->GetBuffer(frames, &bufOut);
            if (FAILED(res))
            {
                m_rebuild = true;
                ++attempt;
                continue;
            }

            for (size_t f=0 ; f<frames ;)
            {
                if (m_curBufFrame == m_5msFrames)
                {
                    _pumpAndMixVoices(m_5msFrames, m_5msBuffer.data());
                    m_curBufFrame = 0;
                }

                size_t remRenderFrames = std::min(frames - f, m_5msFrames - m_curBufFrame);
                if (remRenderFrames)
                {
                    memmove(reinterpret_cast<float*>(bufOut) + m_mixInfo.m_channelMap.m_channelCount * f,
                            &m_5msBuffer[m_curBufFrame * m_mixInfo.m_channelMap.m_channelCount],
                            remRenderFrames * m_mixInfo.m_channelMap.m_channelCount * sizeof(float));
                    m_curBufFrame += remRenderFrames;
                    f += remRenderFrames;
                }
            }

            res = m_renderClient->ReleaseBuffer(frames, 0);
            if (FAILED(res))
            {
                m_rebuild = true;
                ++attempt;
                continue;
            }

            break;
        }
    }

    std::vector<std::pair<std::string, std::string>> enumerateMIDIDevices() const
    {
        std::vector<std::pair<std::string, std::string>> ret;

        UINT numInDevices = midiInGetNumDevs();
        UINT numOutDevices = midiOutGetNumDevs();
        ret.reserve(numInDevices + numOutDevices);

        for (UINT i=0 ; i<numInDevices ; ++i)
        {
            char name[256];
            snprintf(name, 256, "in%u", i);

            MIDIINCAPS caps;
            if (FAILED(midiInGetDevCaps(i, &caps, sizeof(caps))))
                continue;

#ifdef UNICODE
            ret.push_back(std::make_pair(std::string(name), WCSTMBS(caps.szPname)));
#else
            ret.push_back(std::make_pair(std::string(name), std::string(caps.szPname)));
#endif
        }

        for (UINT i=0 ; i<numOutDevices ; ++i)
        {
            char name[256];
            snprintf(name, 256, "out%u", i);

            MIDIOUTCAPS caps;
            if (FAILED(midiOutGetDevCaps(i, &caps, sizeof(caps))))
                continue;

#ifdef UNICODE
            ret.push_back(std::make_pair(std::string(name), WCSTMBS(caps.szPname)));
#else
            ret.push_back(std::make_pair(std::string(name), std::string(caps.szPname)));
#endif
        }

        return ret;
    }

    static void CALLBACK MIDIReceiveProc(HMIDIIN   hMidiIn,
                                         UINT      wMsg,
                                         IMIDIReceiver* dwInstance,
                                         DWORD_PTR dwParam1,
                                         DWORD_PTR dwParam2)
    {
        if (wMsg == MIM_DATA)
        {
            uint8_t (&ptr)[3] = reinterpret_cast<uint8_t(&)[3]>(dwParam1);
            std::vector<uint8_t> bytes(std::cbegin(ptr), std::cend(ptr));
            dwInstance->m_receiver(std::move(bytes), dwParam2 / 1000.0);
        }
    }

    struct MIDIIn : public IMIDIIn
    {
        HMIDIIN m_midi = 0;

        MIDIIn(bool virt, ReceiveFunctor&& receiver)
        : IMIDIIn(virt, std::move(receiver)) {}

        ~MIDIIn()
        {
            midiInClose(m_midi);
        }

        std::string description() const
        {
            UINT id = 0;
            midiInGetID(m_midi, &id);
            MIDIINCAPS caps;
            if (FAILED(midiInGetDevCaps(id, &caps, sizeof(caps))))
                return {};

#ifdef UNICODE
            return WCSTMBS(caps.szPname);
#else
            return caps.szPname;
#endif
        }
    };

    struct MIDIOut : public IMIDIOut
    {
        HMIDIOUT m_midi = 0;
        HMIDISTRM m_strm = 0;
        uint8_t m_buf[512];
        MIDIHDR m_hdr = {};

        MIDIOut(bool virt) : IMIDIOut(virt) {}

        void prepare()
        {
            UINT id = 0;
            midiOutGetID(m_midi, &id);

            m_hdr.lpData = reinterpret_cast<LPSTR>(m_buf);
            m_hdr.dwBufferLength = 512;
            m_hdr.dwFlags = MHDR_ISSTRM;
            midiOutPrepareHeader(m_midi, &m_hdr, sizeof(m_hdr));
            midiStreamOpen(&m_strm, &id, 1, NULL, NULL, CALLBACK_NULL);
        }

        ~MIDIOut()
        {
            midiStreamClose(m_strm);
            midiOutUnprepareHeader(m_midi, &m_hdr, sizeof(m_hdr));
            midiOutClose(m_midi);
        }

        std::string description() const
        {
            UINT id = 0;
            midiOutGetID(m_midi, &id);
            MIDIOUTCAPS caps;
            if (FAILED(midiOutGetDevCaps(id, &caps, sizeof(caps))))
                return {};

#ifdef UNICODE
            return WCSTMBS(caps.szPname);
#else
            return caps.szPname;
#endif
        }

        size_t send(const void* buf, size_t len) const
        {
            memcpy(const_cast<MIDIOut*>(this)->m_buf, buf, std::min(len, size_t(512)));
            const_cast<MIDIOut*>(this)->m_hdr.dwBytesRecorded = len;
            midiStreamOut(m_strm, LPMIDIHDR(&m_hdr), sizeof(m_hdr));
            return len;
        }
    };

    struct MIDIInOut : public IMIDIInOut
    {
        HMIDIIN m_midiIn = 0;
        HMIDIOUT m_midiOut = 0;
        HMIDISTRM m_strm = 0;
        uint8_t m_buf[512];
        MIDIHDR m_hdr = {};

        MIDIInOut(bool virt, ReceiveFunctor&& receiver)
        : IMIDIInOut(virt, std::move(receiver)) {}

        void prepare()
        {
            UINT id = 0;
            midiOutGetID(m_midiOut, &id);

            m_hdr.lpData = reinterpret_cast<LPSTR>(m_buf);
            m_hdr.dwBufferLength = 512;
            m_hdr.dwFlags = MHDR_ISSTRM;
            midiOutPrepareHeader(m_midiOut, &m_hdr, sizeof(m_hdr));
            midiStreamOpen(&m_strm, &id, 1, NULL, NULL, CALLBACK_NULL);
        }

        ~MIDIInOut()
        {
            midiInClose(m_midiIn);
            midiStreamClose(m_strm);
            midiOutUnprepareHeader(m_midiOut, &m_hdr, sizeof(m_hdr));
            midiOutClose(m_midiOut);
        }

        std::string description() const
        {
            UINT id = 0;
            midiOutGetID(m_midiOut, &id);
            MIDIOUTCAPS caps;
            if (FAILED(midiOutGetDevCaps(id, &caps, sizeof(caps))))
                return {};

#ifdef UNICODE
            return WCSTMBS(caps.szPname);
#else
            return caps.szPname;
#endif
        }

        size_t send(const void* buf, size_t len) const
        {
            memcpy(const_cast<uint8_t*>(m_buf), buf, std::min(len, size_t(512)));
            const_cast<MIDIHDR&>(m_hdr).dwBytesRecorded = len;
            midiStreamOut(m_strm, LPMIDIHDR(&m_hdr), sizeof(m_hdr));
            return len;
        }
    };

    std::unique_ptr<IMIDIIn> newVirtualMIDIIn(ReceiveFunctor&& receiver)
    {
        return {};
    }

    std::unique_ptr<IMIDIOut> newVirtualMIDIOut()
    {
        return {};
    }

    std::unique_ptr<IMIDIInOut> newVirtualMIDIInOut(ReceiveFunctor&& receiver)
    {
        return {};
    }

    std::unique_ptr<IMIDIIn> newRealMIDIIn(const char* name, ReceiveFunctor&& receiver)
    {
        if (strcmp(name, "in"))
            return {};
        long id = strtol(name + 2, nullptr, 10);

        std::unique_ptr<IMIDIIn> ret = std::make_unique<MIDIIn>(false, std::move(receiver));
        if (!ret)
            return {};

        if (FAILED(midiInOpen(&static_cast<MIDIIn&>(*ret).m_midi, id, DWORD_PTR(MIDIReceiveProc),
                              DWORD_PTR(static_cast<IMIDIReceiver*>(ret.get())), CALLBACK_FUNCTION)))
            return {};

        return ret;
    }

    std::unique_ptr<IMIDIOut> newRealMIDIOut(const char* name)
    {
        if (strcmp(name, "out"))
            return {};
        long id = strtol(name + 3, nullptr, 10);

        std::unique_ptr<IMIDIOut> ret = std::make_unique<MIDIOut>(false);
        if (!ret)
            return {};

        if (FAILED(midiOutOpen(&static_cast<MIDIOut&>(*ret).m_midi, id, NULL,
                               NULL, CALLBACK_NULL)))
            return {};

        static_cast<MIDIOut&>(*ret).prepare();
        return ret;
    }

    std::unique_ptr<IMIDIInOut> newRealMIDIInOut(const char* name, ReceiveFunctor&& receiver)
    {
        const char* in = strstr(name, "in");
        const char* out = strstr(name, "out");

        if (!in || !out)
            return {};

        long inId = strtol(in + 2, nullptr, 10);
        long outId = strtol(out + 3, nullptr, 10);

        std::unique_ptr<IMIDIInOut> ret = std::make_unique<MIDIInOut>(false, std::move(receiver));
        if (!ret)
            return {};

        if (FAILED(midiInOpen(&static_cast<MIDIInOut&>(*ret).m_midiIn, inId, DWORD_PTR(MIDIReceiveProc),
                              DWORD_PTR(static_cast<IMIDIReceiver*>(ret.get())), CALLBACK_FUNCTION)))
            return {};

        if (FAILED(midiOutOpen(&static_cast<MIDIInOut&>(*ret).m_midiOut, outId, NULL,
                               NULL, CALLBACK_NULL)))
            return {};

        static_cast<MIDIInOut&>(*ret).prepare();
        return ret;
    }

    bool useMIDILock() const {return true;}
};

std::unique_ptr<IAudioVoiceEngine> NewAudioVoiceEngine()
{
    std::unique_ptr<IAudioVoiceEngine> ret = std::make_unique<WASAPIAudioVoiceEngine>();
    if (!static_cast<WASAPIAudioVoiceEngine&>(*ret).m_device)
        return {};
    return ret;
}

}
