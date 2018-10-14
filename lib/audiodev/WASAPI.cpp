#include "../win/Win32Common.hpp"
#include "AudioVoiceEngine.hpp"
#include "logvisor/logvisor.hpp"
#include "boo/IApplication.hpp"

#include <Mmdeviceapi.h>
#include <Audioclient.h>
#include <mmsystem.h>
#include <Functiondiscoverykeys_devpkey.h>

#include <iterator>

#ifdef TE_VIRTUAL_MIDI
#include <teVirtualMIDI.h>
typedef LPVM_MIDI_PORT (CALLBACK *pfnvirtualMIDICreatePortEx2)
( LPCWSTR portName, LPVM_MIDI_DATA_CB callback, DWORD_PTR dwCallbackInstance, DWORD maxSysexLength, DWORD flags );
typedef void (CALLBACK *pfnvirtualMIDIClosePort)( LPVM_MIDI_PORT midiPort );
typedef BOOL (CALLBACK *pfnvirtualMIDISendData)( LPVM_MIDI_PORT midiPort, LPBYTE midiDataBytes, DWORD length );
typedef LPCWSTR (CALLBACK *pfnvirtualMIDIGetDriverVersion)( PWORD major, PWORD minor, PWORD release, PWORD build );
static pfnvirtualMIDICreatePortEx2 virtualMIDICreatePortEx2PROC = nullptr;
static pfnvirtualMIDIClosePort virtualMIDIClosePortPROC = nullptr;
static pfnvirtualMIDISendData virtualMIDISendDataPROC = nullptr;
static pfnvirtualMIDIGetDriverVersion virtualMIDIGetDriverVersionPROC = nullptr;
static double PerfFrequency = 0.0;
#endif

#if !WINDOWS_STORE
const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
#else
using namespace Windows::Media::Devices;
#endif
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
#if !WINDOWS_STORE
    ComPtr<IMMDeviceEnumerator> m_enumerator;
    ComPtr<IMMDevice> m_device;
#else
    bool m_ready = false;
#endif
    ComPtr<IAudioClient> m_audClient;
    ComPtr<IAudioRenderClient> m_renderClient;
    std::string m_sinkName;

    size_t m_curBufFrame = 0;
    std::vector<float> m_5msBuffer;

#if !WINDOWS_STORE
    struct NotificationClient final : public IMMNotificationClient
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
#endif

    void _buildAudioRenderClient()
    {
#if !WINDOWS_STORE
        if (!m_device)
        {
            if (FAILED(m_enumerator->GetDevice(MBSTWCS(m_sinkName.c_str()).c_str(), &m_device)))
            {
                Log.report(logvisor::Error, "unable to obtain audio device %s", m_sinkName.c_str());
                m_device.Reset();
                return;
            }
        }

        if (FAILED(m_device->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, &m_audClient)))
        {
            Log.report(logvisor::Error, L"unable to create audio client from device");
            m_device.Reset();
            return;
        }
#endif

        WAVEFORMATEXTENSIBLE* pwfx;
        if (FAILED(m_audClient->GetMixFormat((WAVEFORMATEX**)&pwfx)))
        {
            Log.report(logvisor::Error, L"unable to obtain audio mix format from device");
#if !WINDOWS_STORE
            m_device.Reset();
#endif
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
            Log.report(logvisor::Error, L"unable to initialize audio client");
#if !WINDOWS_STORE
            m_device.Reset();
#endif
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
#if !WINDOWS_STORE
                m_device.Reset();
#endif
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
                Log.report(logvisor::Error, L"unsupported floating-point bits-per-sample %d", pwfx->Format.wBitsPerSample);
#if !WINDOWS_STORE
                m_device.Reset();
#endif
                return;
            }
        }

        CoTaskMemFree(pwfx);

        UINT32 bufferFrameCount;
        if (FAILED(m_audClient->GetBufferSize(&bufferFrameCount)))
        {
            Log.report(logvisor::Error, L"unable to get audio buffer frame count");
#if !WINDOWS_STORE
            m_device.Reset();
#endif
            return;
        }
        m_mixInfo.m_periodFrames = bufferFrameCount;

        if (FAILED(m_audClient->GetService(IID_IAudioRenderClient, &m_renderClient)))
        {
            Log.report(logvisor::Error, L"unable to create audio render client");
#if !WINDOWS_STORE
            m_device.Reset();
#endif
            return;
        }
    }

#if WINDOWS_STORE
    struct CompletionHandler : IActivateAudioInterfaceCompletionHandler
    {
        WASAPIAudioVoiceEngine& e;
        LONG _cRef = 1;

        CompletionHandler(WASAPIAudioVoiceEngine& e) : e(e) {}
        HRESULT ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation)
        {
            return e.ActivateCompleted(operation);
        }

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
            else if (__uuidof(IActivateAudioInterfaceCompletionHandler) == riid)
            {
                AddRef();
                *ppvInterface = (IActivateAudioInterfaceCompletionHandler*)this;
            }
            else
            {
                *ppvInterface = NULL;
                return E_NOINTERFACE;
            }
            return S_OK;
        }
    } m_completion = {*this};
    HRESULT ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation)
    {
        ComPtr<IUnknown> punkAudioInterface;
        HRESULT hrActivateResult;
        operation->GetActivateResult(&hrActivateResult, &punkAudioInterface);
        punkAudioInterface.As<IAudioClient>(&m_audClient);
        _buildAudioRenderClient();
        m_ready = true;
        return ERROR_SUCCESS;
    }
#endif

    WASAPIAudioVoiceEngine()
#if !WINDOWS_STORE
    : m_notificationClient(*this)
#endif
    {
#if !WINDOWS_STORE
#ifdef TE_VIRTUAL_MIDI
        HMODULE virtualMidiModule;
        if (!virtualMIDICreatePortEx2PROC && (virtualMidiModule = LoadLibraryW(L"teVirtualMIDI64.dll")))
        {
            virtualMIDICreatePortEx2PROC = (pfnvirtualMIDICreatePortEx2)GetProcAddress(virtualMidiModule, "virtualMIDICreatePortEx2");
            virtualMIDIClosePortPROC = (pfnvirtualMIDIClosePort)GetProcAddress(virtualMidiModule, "virtualMIDIClosePort");
            virtualMIDISendDataPROC = (pfnvirtualMIDISendData)GetProcAddress(virtualMidiModule, "virtualMIDISendData");
            virtualMIDIGetDriverVersionPROC = (pfnvirtualMIDIGetDriverVersion)GetProcAddress(virtualMidiModule, "virtualMIDIGetDriverVersion");
            LARGE_INTEGER pf;
            QueryPerformanceFrequency(&pf);
            PerfFrequency = double(pf.QuadPart);
        }
#endif

        /* Enumerate default audio device */
        if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr,
                                    CLSCTX_ALL, IID_IMMDeviceEnumerator,
                                    &m_enumerator)))
        {
            Log.report(logvisor::Error, L"unable to create MMDeviceEnumerator instance");
            return;
        }

        if (FAILED(m_enumerator->RegisterEndpointNotificationCallback(&m_notificationClient)))
        {
            Log.report(logvisor::Error, L"unable to register multimedia event callback");
            m_device.Reset();
            return;
        }

        if (FAILED(m_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_device)))
        {
            Log.report(logvisor::Error, L"unable to obtain default audio device");
            m_device.Reset();
            return;
        }
        LPWSTR sinkName = nullptr;
        m_device->GetId(&sinkName);
        m_sinkName = WCSTMBS(sinkName);
        CoTaskMemFree(sinkName);

        _buildAudioRenderClient();
#else
        auto deviceIdStr = MediaDevice::GetDefaultAudioRenderId(Windows::Media::Devices::AudioDeviceRole::Default);
        ComPtr<IActivateAudioInterfaceAsyncOperation> asyncOp;
        ActivateAudioInterfaceAsync(deviceIdStr->Data(), __uuidof(IAudioClient3), nullptr, &m_completion, &asyncOp);
#endif
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

         _resetSampleRate();
    }

    void pumpAndMixVoices()
    {
#if WINDOWS_STORE
        if (!m_ready)
            return;
#else
        if (!m_device)
            return;
#endif

        int attempt = 0;
        while (true)
        {
            if (attempt >= 10)
                Log.report(logvisor::Fatal, L"unable to setup AudioRenderClient");

            if (m_rebuild)
            {
                m_device.Reset();
                _rebuildAudioRenderClient();
            }

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

    std::string getCurrentAudioOutput() const
    {
        return m_sinkName;
    }

    bool setCurrentAudioOutput(const char* name)
    {
        ComPtr<IMMDevice> newDevice;
        if (FAILED(m_enumerator->GetDevice(MBSTWCS(name).c_str(), &newDevice)))
        {
            Log.report(logvisor::Error, "unable to obtain audio device %s", name);
            return false;
        }
        m_device = newDevice;
        m_sinkName = name;
        _rebuildAudioRenderClient();
        return true;
    }

    std::vector<std::pair<std::string, std::string>> enumerateAudioOutputs() const
    {
        std::vector<std::pair<std::string, std::string>> ret;

        ComPtr<IMMDeviceCollection> collection;
        if (FAILED(m_enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection)))
        {
            Log.report(logvisor::Error, L"unable to enumerate audio outputs");
            return ret;
        }

        UINT count = 0;
        collection->GetCount(&count);
        for (UINT i = 0; i < count; ++i)
        {
            ComPtr<IMMDevice> device;
            collection->Item(i, &device);
            LPWSTR devName;
            device->GetId(&devName);
            ComPtr<IPropertyStore> props;
            device->OpenPropertyStore(STGM_READ, &props);
            PROPVARIANT val = {};
            props->GetValue(PKEY_Device_FriendlyName, &val);
            std::string friendlyName;
            if (val.vt == VT_LPWSTR)
                friendlyName = WCSTMBS(val.pwszVal);
            ret.emplace_back(WCSTMBS(devName), std::move(friendlyName));
        }

        return ret;
    }

#if !WINDOWS_STORE
    std::vector<std::pair<std::string, std::string>> enumerateMIDIInputs() const
    {
        std::vector<std::pair<std::string, std::string>> ret;

        UINT numInDevices = midiInGetNumDevs();
        ret.reserve(numInDevices);

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

#if 0
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
#endif

        return ret;
    }

    bool supportsVirtualMIDIIn() const
    {
#ifdef TE_VIRTUAL_MIDI
        WORD major, minor, release, build;
        return virtualMIDIGetDriverVersionPROC &&
               virtualMIDIGetDriverVersionPROC(&major, &minor, &release, &build) != nullptr;
#else
        return false;
#endif
    }

#ifdef TE_VIRTUAL_MIDI
    static void CALLBACK VirtualMIDIReceiveProc(LPVM_MIDI_PORT midiPort,
                                                LPBYTE midiDataBytes,
                                                DWORD length,
                                                IMIDIReceiver* dwInstance)
    {
        std::vector<uint8_t> bytes;
        bytes.resize(length);
        memcpy(&bytes[0], midiDataBytes, length);

        double timestamp;
        LARGE_INTEGER perf;
        QueryPerformanceCounter(&perf);
        timestamp = perf.QuadPart / PerfFrequency;

        dwInstance->m_receiver(std::move(bytes), timestamp);
    }
#endif

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

#ifdef TE_VIRTUAL_MIDI
    struct VMIDIIn : public IMIDIIn
    {
        LPVM_MIDI_PORT m_midi = 0;

        VMIDIIn(WASAPIAudioVoiceEngine* parent, ReceiveFunctor&& receiver)
        : IMIDIIn(parent, true, std::move(receiver)) {}

        ~VMIDIIn()
        {
            virtualMIDIClosePortPROC(m_midi);
        }

        std::string description() const
        {
            return "Virtual MIDI-In";
        }
    };

    struct VMIDIOut : public IMIDIOut
    {
        LPVM_MIDI_PORT m_midi = 0;

        VMIDIOut(WASAPIAudioVoiceEngine* parent) : IMIDIOut(parent, true) {}

        ~VMIDIOut()
        {
            virtualMIDIClosePortPROC(m_midi);
        }

        std::string description() const
        {
            return "Virtual MIDI-Out";
        }

        size_t send(const void* buf, size_t len) const
        {
            return virtualMIDISendDataPROC(m_midi, (LPBYTE)buf, len) ? len : 0;
        }
    };

    struct VMIDIInOut : public IMIDIInOut
    {
        LPVM_MIDI_PORT m_midi = 0;

        VMIDIInOut(WASAPIAudioVoiceEngine* parent, ReceiveFunctor&& receiver)
        : IMIDIInOut(parent, true, std::move(receiver)) {}

        ~VMIDIInOut()
        {
            virtualMIDIClosePortPROC(m_midi);
        }

        std::string description() const
        {
            return "Virtual MIDI-In/Out";
        }

        size_t send(const void* buf, size_t len) const
        {
            return virtualMIDISendDataPROC(m_midi, (LPBYTE)buf, len) ? len : 0;
        }
    };
#endif

    struct MIDIIn : public IMIDIIn
    {
        HMIDIIN m_midi = 0;

        MIDIIn(WASAPIAudioVoiceEngine* parent, ReceiveFunctor&& receiver)
        : IMIDIIn(parent, false, std::move(receiver)) {}

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

        MIDIOut(WASAPIAudioVoiceEngine* parent) : IMIDIOut(parent, false) {}

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

        MIDIInOut(WASAPIAudioVoiceEngine* parent, ReceiveFunctor&& receiver)
        : IMIDIInOut(parent, false, std::move(receiver)) {}

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
#ifdef TE_VIRTUAL_MIDI
        if (!virtualMIDICreatePortEx2PROC)
            return {};

        std::unique_ptr<IMIDIIn> ret = std::make_unique<VMIDIIn>(this, std::move(receiver));
        if (!ret)
            return {};

        SystemString name = SystemString(APP->getFriendlyName()) + _SYS_STR(" MIDI-In");
        auto port = virtualMIDICreatePortEx2PROC(name.c_str(), LPVM_MIDI_DATA_CB(VirtualMIDIReceiveProc),
                                                 DWORD_PTR(static_cast<IMIDIReceiver*>(ret.get())), 512,
                                                 TE_VM_FLAGS_PARSE_RX | TE_VM_FLAGS_INSTANTIATE_RX_ONLY);
        if (!port)
            return {};
        static_cast<VMIDIIn&>(*ret).m_midi = port;
        return ret;
#else
        return {};
#endif
    }

    std::unique_ptr<IMIDIOut> newVirtualMIDIOut()
    {
#ifdef TE_VIRTUAL_MIDI
        if (!virtualMIDICreatePortEx2PROC)
            return {};

        std::unique_ptr<IMIDIOut> ret = std::make_unique<VMIDIOut>(this);
        if (!ret)
            return {};

        SystemString name = SystemString(APP->getFriendlyName()) + _SYS_STR(" MIDI-Out");
        auto port = virtualMIDICreatePortEx2PROC(name.c_str(), nullptr, 0, 512,
                                                 TE_VM_FLAGS_PARSE_TX | TE_VM_FLAGS_INSTANTIATE_TX_ONLY);
        if (!port)
            return {};
        static_cast<VMIDIOut&>(*ret).m_midi = port;
        return ret;
#else
        return {};
#endif
    }

    std::unique_ptr<IMIDIInOut> newVirtualMIDIInOut(ReceiveFunctor&& receiver)
    {
#ifdef TE_VIRTUAL_MIDI
        if (!virtualMIDICreatePortEx2PROC)
            return {};

        std::unique_ptr<IMIDIInOut> ret = std::make_unique<VMIDIInOut>(this, std::move(receiver));
        if (!ret)
            return {};

        SystemString name = SystemString(APP->getFriendlyName()) + _SYS_STR(" MIDI-In/Out");
        auto port = virtualMIDICreatePortEx2PROC(name.c_str(), LPVM_MIDI_DATA_CB(VirtualMIDIReceiveProc),
                                                 DWORD_PTR(static_cast<IMIDIReceiver*>(ret.get())), 512,
                                                 TE_VM_FLAGS_SUPPORTED);
        if (!port)
            return {};
        static_cast<VMIDIInOut&>(*ret).m_midi = port;
        return ret;
#else
        return {};
#endif
    }

    std::unique_ptr<IMIDIIn> newRealMIDIIn(const char* name, ReceiveFunctor&& receiver)
    {
        if (strncmp(name, "in", 2))
            return {};
        long id = strtol(name + 2, nullptr, 10);

        std::unique_ptr<IMIDIIn> ret = std::make_unique<MIDIIn>(this, std::move(receiver));
        if (!ret)
            return {};

        if (FAILED(midiInOpen(&static_cast<MIDIIn&>(*ret).m_midi, id, DWORD_PTR(MIDIReceiveProc),
                              DWORD_PTR(static_cast<IMIDIReceiver*>(ret.get())), CALLBACK_FUNCTION)))
            return {};
        midiInStart(static_cast<MIDIIn&>(*ret).m_midi);

        return ret;
    }

    std::unique_ptr<IMIDIOut> newRealMIDIOut(const char* name)
    {
        if (strncmp(name, "out", 3))
            return {};
        long id = strtol(name + 3, nullptr, 10);

        std::unique_ptr<IMIDIOut> ret = std::make_unique<MIDIOut>(this);
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

        std::unique_ptr<IMIDIInOut> ret = std::make_unique<MIDIInOut>(this, std::move(receiver));
        if (!ret)
            return {};

        if (FAILED(midiInOpen(&static_cast<MIDIInOut&>(*ret).m_midiIn, inId, DWORD_PTR(MIDIReceiveProc),
                              DWORD_PTR(static_cast<IMIDIReceiver*>(ret.get())), CALLBACK_FUNCTION)))
            return {};
        midiInStart(static_cast<MIDIInOut&>(*ret).m_midiIn);

        if (FAILED(midiOutOpen(&static_cast<MIDIInOut&>(*ret).m_midiOut, outId, NULL,
                               NULL, CALLBACK_NULL)))
            return {};

        static_cast<MIDIInOut&>(*ret).prepare();
        return ret;
    }

    bool useMIDILock() const {return true;}
#else
    std::vector<std::pair<std::string, std::string>> enumerateMIDIDevices() const
    {
        return {};
    }

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
        return {};
    }

    std::unique_ptr<IMIDIOut> newRealMIDIOut(const char* name)
    {
        return {};
    }

    std::unique_ptr<IMIDIInOut> newRealMIDIInOut(const char* name, ReceiveFunctor&& receiver)
    {
        return {};
    }

    bool useMIDILock() const {return false;}
#endif
};

std::unique_ptr<IAudioVoiceEngine> NewAudioVoiceEngine()
{
    return std::make_unique<WASAPIAudioVoiceEngine>();
}

}
