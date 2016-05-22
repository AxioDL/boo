#include "AudioVoiceEngine.hpp"
#include "logvisor/logvisor.hpp"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreMIDI/CoreMIDI.h>
#include <CoreAudio/HostTime.h>

#include <mutex>
#include <condition_variable>

namespace boo
{
static logvisor::Module Log("boo::AQS");

static AudioChannel AQSChannelToBooChannel(AudioChannelLabel ch)
{
    switch (ch)
    {
    case kAudioChannelLabel_Left:
        return AudioChannel::FrontLeft;
    case kAudioChannelLabel_Right:
        return AudioChannel::FrontRight;
    case kAudioChannelLabel_LeftSurround:
        return AudioChannel::RearLeft;
    case kAudioChannelLabel_RightSurround:
        return AudioChannel::RearRight;
    case kAudioChannelLabel_Center:
        return AudioChannel::FrontCenter;
    case kAudioChannelLabel_LFEScreen:
        return AudioChannel::LFE;
    case kAudioChannelLabel_LeftSurroundDirect:
        return AudioChannel::RearLeft;
    case kAudioChannelLabel_RightSurroundDirect:
        return AudioChannel::SideRight;
    }
    return AudioChannel::Unknown;
}

struct AQSAudioVoiceEngine : BaseAudioVoiceEngine
{
    AudioQueueRef m_queue = nullptr;
    AudioQueueBufferRef m_buffers[3];
    size_t m_frameBytes;

    MIDIClientRef m_midiClient;

    std::mutex m_engineMutex;
    std::condition_variable m_engineEnterCv;
    std::condition_variable m_engineLeaveCv;
    bool m_cbWaiting = false;

    static void Callback(AQSAudioVoiceEngine* engine, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer)
    {
        std::unique_lock<std::mutex> lk(engine->m_engineMutex);
        engine->m_cbWaiting = true;
        engine->m_engineEnterCv.wait(lk);
        engine->m_cbWaiting = false;

        engine->_pumpAndMixVoices(engine->m_mixInfo.m_periodFrames, reinterpret_cast<int32_t*>(inBuffer->mAudioData));
        inBuffer->mAudioDataByteSize = engine->m_frameBytes;
        AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, nullptr);
        
        engine->m_engineLeaveCv.notify_one();
    }

    static void DummyCallback(AQSAudioVoiceEngine* engine, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer) {}

    AudioChannelSet _getAvailableSet()
    {
        const unsigned chCount = 8;
        AudioStreamBasicDescription desc = {};
        desc.mSampleRate = 96000;
        desc.mFormatID = kAudioFormatLinearPCM;
        desc.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger;
        desc.mBytesPerPacket = chCount * 4;
        desc.mFramesPerPacket = 1;
        desc.mBytesPerFrame = chCount * 4;
        desc.mChannelsPerFrame = chCount;
        desc.mBitsPerChannel = 32;

        AudioQueueRef queue;
        if (AudioQueueNewOutput(&desc, AudioQueueOutputCallback(DummyCallback),
                                this, nullptr, nullptr, 0, &queue))
        {
            Log.report(logvisor::Error, "unable to create output audio queue");
            return AudioChannelSet::Unknown;
        }

        UInt32 hwChannels;
        UInt32 channelsSz = sizeof(UInt32);
        if (AudioQueueGetProperty(queue, kAudioQueueDeviceProperty_NumberChannels, &hwChannels, &channelsSz))
        {
            Log.report(logvisor::Error, "unable to get channel count from audio queue");
            AudioQueueDispose(queue, true);
            return AudioChannelSet::Unknown;
        }

        AudioQueueDispose(queue, true);

        switch (hwChannels)
        {
        case 2:
            return AudioChannelSet::Stereo;
        case 4:
            return AudioChannelSet::Quad;
        case 6:
            return AudioChannelSet::Surround51;
        case 8:
            return AudioChannelSet::Surround71;
        default: break;
        }

        return AudioChannelSet::Unknown;
    }

    std::vector<std::pair<std::string, std::string>> enumerateMIDIDevices() const
    {
        std::vector<std::pair<std::string, std::string>> ret;

        ItemCount numDevices = MIDIGetNumberOfDevices();
        ret.reserve(numDevices);
        for (ItemCount i=0 ; i<numDevices ; ++i)
        {
            MIDIDeviceRef dev = MIDIGetDevice(i);
            if (!dev)
                continue;

            CFStringRef idstr;
            if (MIDIObjectGetStringProperty(dev, kMIDIPropertyDeviceID, &idstr))
                continue;

            CFStringRef namestr;
            if (MIDIObjectGetStringProperty(dev, kMIDIPropertyDisplayName, &namestr))
            {
                CFRelease(idstr);
                continue;
            }

            ret.push_back(std::make_pair(std::string(CFStringGetCStringPtr(idstr, kCFStringEncodingUTF8)),
                                         std::string(CFStringGetCStringPtr(namestr, kCFStringEncodingUTF8))));

            CFRelease(idstr);
            CFRelease(namestr);
        }

        return ret;
    }

    static MIDIDeviceRef LookupMIDIDevice(const char* name)
    {
        ItemCount numDevices = MIDIGetNumberOfDevices();
        for (ItemCount i=0 ; i<numDevices ; ++i)
        {
            MIDIDeviceRef dev = MIDIGetDevice(i);
            if (!dev)
                continue;

            CFStringRef idstr;
            if (MIDIObjectGetStringProperty(dev, kMIDIPropertyDeviceID, &idstr))
                continue;

            if (!strcmp(CFStringGetCStringPtr(idstr, kCFStringEncodingUTF8), name))
            {
                CFRelease(idstr);
                return dev;
            }
            CFRelease(idstr);
        }

        return {};
    }

    static MIDIEndpointRef LookupMIDISource(const char* name)
    {
        MIDIDeviceRef dev = LookupMIDIDevice(name);
        if (!dev)
            return {};

        ItemCount numEnt = MIDIDeviceGetNumberOfEntities(dev);
        for (ItemCount i=0 ; i<numEnt ; ++i)
        {
            MIDIEntityRef ent = MIDIDeviceGetEntity(dev, i);
            if (ent)
            {
                ItemCount numSrc = MIDIEntityGetNumberOfSources(ent);
                for (ItemCount s=0 ; s<numSrc ; ++s)
                {
                    MIDIEndpointRef src = MIDIEntityGetSource(ent, s);
                    if (src)
                        return src;
                }
            }
        }

        return {};
    }

    static MIDIEndpointRef LookupMIDIDest(const char* name)
    {
        MIDIDeviceRef dev = LookupMIDIDevice(name);
        if (!dev)
            return {};

        ItemCount numEnt = MIDIDeviceGetNumberOfEntities(dev);
        for (ItemCount i=0 ; i<numEnt ; ++i)
        {
            MIDIEntityRef ent = MIDIDeviceGetEntity(dev, i);
            if (ent)
            {
                ItemCount numDest = MIDIEntityGetNumberOfDestinations(ent);
                for (ItemCount d=0 ; d<numDest ; ++d)
                {
                    MIDIEndpointRef dst = MIDIEntityGetDestination(ent, d);
                    if (dst)
                        return dst;
                }
            }
        }

        return {};
    }

    static void MIDIReceiveProc(const MIDIPacketList* pktlist,
                                IMIDIReceiver* readProcRefCon,
                                void*)
    {
        const MIDIPacket* packet = &pktlist->packet[0];
        for (int i=0 ; i<pktlist->numPackets ; ++i)
        {
            std::vector<uint8_t> bytes(std::cbegin(packet->data), std::cbegin(packet->data) + packet->length);
            readProcRefCon->m_receiver(std::move(bytes));
            packet = MIDIPacketNext(packet);
        }
    }

    struct MIDIIn : public IMIDIIn
    {
        MIDIEndpointRef m_midi = 0;
        MIDIPortRef m_midiPort = 0;

        MIDIIn(bool virt, ReceiveFunctor&& receiver)
        : IMIDIIn(virt, std::move(receiver)) {}

        ~MIDIIn()
        {
            if (m_midi)
                MIDIEndpointDispose(m_midi);
            if (m_midiPort)
                MIDIPortDispose(m_midiPort);
        }

        std::string description() const
        {
            CFStringRef namestr;
            if (MIDIObjectGetStringProperty(m_midi, kMIDIPropertyDisplayName, &namestr))
                return {};

            std::string ret(CFStringGetCStringPtr(namestr, kCFStringEncodingUTF8));
            CFRelease(namestr);
            return ret;
        }
    };

    struct MIDIOut : public IMIDIOut
    {
        MIDIEndpointRef m_midi = 0;
        MIDIPortRef m_midiPort = 0;

        MIDIOut(bool virt)
        : IMIDIOut(virt) {}

        ~MIDIOut()
        {
            if (m_midi)
                MIDIEndpointDispose(m_midi);
            if (m_midiPort)
                MIDIPortDispose(m_midiPort);
        }

        std::string description() const
        {
            CFStringRef namestr;
            if (MIDIObjectGetStringProperty(m_midi, kMIDIPropertyDisplayName, &namestr))
                return {};

            std::string ret(CFStringGetCStringPtr(namestr, kCFStringEncodingUTF8));
            CFRelease(namestr);
            return ret;
        }

        size_t send(const void* buf, size_t len) const
        {
            union
            {
                MIDIPacketList head;
                Byte storage[512];
            } list;
            MIDIPacket* curPacket = MIDIPacketListInit(&list.head);
            if (MIDIPacketListAdd(&list.head, sizeof(list), curPacket, AudioGetCurrentHostTime(),
                                  len, reinterpret_cast<const Byte*>(buf)))
            {
                if (m_midiPort)
                    MIDISend(m_midiPort, m_midi, &list.head);
                else
                    MIDIReceived(m_midi, &list.head);
                return len;
            }
            return 0;
        }
    };

    struct MIDIInOut : public IMIDIInOut
    {
        MIDIEndpointRef m_midiIn = 0;
        MIDIPortRef m_midiPortIn = 0;
        MIDIEndpointRef m_midiOut = 0;
        MIDIPortRef m_midiPortOut = 0;

        MIDIInOut(bool virt, ReceiveFunctor&& receiver)
        : IMIDIInOut(virt, std::move(receiver)) {}

        ~MIDIInOut()
        {
            if (m_midiIn)
                MIDIEndpointDispose(m_midiIn);
            if (m_midiPortIn)
                MIDIPortDispose(m_midiPortIn);
            if (m_midiOut)
                MIDIEndpointDispose(m_midiOut);
            if (m_midiPortOut)
                MIDIPortDispose(m_midiPortOut);
        }

        std::string description() const
        {
            CFStringRef namestr;
            if (MIDIObjectGetStringProperty(m_midiIn, kMIDIPropertyDisplayName, &namestr))
                return {};

            std::string ret(CFStringGetCStringPtr(namestr, kCFStringEncodingUTF8));
            CFRelease(namestr);
            return ret;
        }

        size_t send(const void* buf, size_t len) const
        {
            union
            {
                MIDIPacketList head;
                Byte storage[512];
            } list;
            MIDIPacket* curPacket = MIDIPacketListInit(&list.head);
            if (MIDIPacketListAdd(&list.head, sizeof(list), curPacket, AudioGetCurrentHostTime(),
                                  len, reinterpret_cast<const Byte*>(buf)))
            {
                if (m_midiPortOut)
                    MIDISend(m_midiPortOut, m_midiOut, &list.head);
                else
                    MIDIReceived(m_midiOut, &list.head);
                return len;
            }
            return 0;
        }
    };

    unsigned m_midiInCounter = 0;
    unsigned m_midiOutCounter = 0;

    std::unique_ptr<IMIDIIn> newVirtualMIDIIn(ReceiveFunctor&& receiver)
    {
        std::unique_ptr<IMIDIIn> ret = std::make_unique<MIDIIn>(true, std::move(receiver));
        if (!ret)
            return {};

        char name[256];
        snprintf(name, 256, "Boo MIDI Virtual In %u\n", m_midiInCounter++);
        CFStringRef midiName = CFStringCreateWithCStringNoCopy(nullptr, name, kCFStringEncodingUTF8, kCFAllocatorNull);
        OSStatus stat;
        if ((stat = MIDIDestinationCreate(m_midiClient, midiName, MIDIReadProc(MIDIReceiveProc),
                                  static_cast<IMIDIReceiver*>(ret.get()),
                                  &static_cast<MIDIIn&>(*ret).m_midi)))
            ret.reset();
        CFRelease(midiName);

        return ret;
    }

    std::unique_ptr<IMIDIOut> newVirtualMIDIOut()
    {
        std::unique_ptr<IMIDIOut> ret = std::make_unique<MIDIOut>(true);
        if (!ret)
            return {};

        char name[256];
        snprintf(name, 256, "Boo MIDI Virtual Out %u\n", m_midiOutCounter++);
        CFStringRef midiName = CFStringCreateWithCStringNoCopy(nullptr, name, kCFStringEncodingUTF8, kCFAllocatorNull);
        if (MIDISourceCreate(m_midiClient, midiName, &static_cast<MIDIOut&>(*ret).m_midi))
            ret.reset();
        CFRelease(midiName);

        return ret;
    }

    std::unique_ptr<IMIDIInOut> newVirtualMIDIInOut(ReceiveFunctor&& receiver)
    {
        std::unique_ptr<IMIDIInOut> ret = std::make_unique<MIDIInOut>(true, std::move(receiver));
        if (!ret)
            return {};

        char name[256];
        snprintf(name, 256, "Boo MIDI Virtual In %u\n", m_midiInCounter++);
        CFStringRef midiName = CFStringCreateWithCStringNoCopy(nullptr, name, kCFStringEncodingUTF8, kCFAllocatorNull);
        if (MIDIDestinationCreate(m_midiClient, midiName, MIDIReadProc(MIDIReceiveProc),
                                  static_cast<IMIDIReceiver*>(ret.get()),
                                  &static_cast<MIDIInOut&>(*ret).m_midiIn))
            ret.reset();
        CFRelease(midiName);

        if (!ret)
            return {};

        snprintf(name, 256, "Boo MIDI Virtual Out %u\n", m_midiOutCounter++);
        midiName = CFStringCreateWithCStringNoCopy(nullptr, name, kCFStringEncodingUTF8, kCFAllocatorNull);
        if (MIDISourceCreate(m_midiClient, midiName, &static_cast<MIDIInOut&>(*ret).m_midiOut))
            ret.reset();
        CFRelease(midiName);

        return ret;
    }

    std::unique_ptr<IMIDIIn> newRealMIDIIn(const char* name, ReceiveFunctor&& receiver)
    {
        MIDIEndpointRef src = LookupMIDISource(name);
        if (!src)
            return {};

        std::unique_ptr<IMIDIIn> ret = std::make_unique<MIDIIn>(false, std::move(receiver));
        if (!ret)
            return {};

        char mname[256];
        snprintf(mname, 256, "Boo MIDI Real In %u\n", m_midiInCounter++);
        CFStringRef midiName = CFStringCreateWithCStringNoCopy(nullptr, mname, kCFStringEncodingUTF8, kCFAllocatorNull);
        if (MIDIInputPortCreate(m_midiClient, midiName, MIDIReadProc(MIDIReceiveProc),
                                static_cast<IMIDIReceiver*>(ret.get()),
                                &static_cast<MIDIIn&>(*ret).m_midiPort))
            ret.reset();
        else
            MIDIPortConnectSource(static_cast<MIDIIn&>(*ret).m_midiPort, src, nullptr);
        CFRelease(midiName);

        return ret;
    }

    std::unique_ptr<IMIDIOut> newRealMIDIOut(const char* name)
    {
        MIDIEndpointRef dst = LookupMIDIDest(name);
        if (!dst)
            return {};

        std::unique_ptr<IMIDIOut> ret = std::make_unique<MIDIOut>(false);
        if (!ret)
            return {};

        char mname[256];
        snprintf(mname, 256, "Boo MIDI Real Out %u\n", m_midiOutCounter++);
        CFStringRef midiName = CFStringCreateWithCStringNoCopy(nullptr, mname, kCFStringEncodingUTF8, kCFAllocatorNull);
        if (MIDIOutputPortCreate(m_midiClient, midiName, &static_cast<MIDIOut&>(*ret).m_midiPort))
            ret.reset();
        else
            static_cast<MIDIOut&>(*ret).m_midi = dst;
        CFRelease(midiName);

        return ret;
    }

    std::unique_ptr<IMIDIInOut> newRealMIDIInOut(const char* name, ReceiveFunctor&& receiver)
    {
        MIDIEndpointRef src = LookupMIDISource(name);
        if (!src)
            return {};

        MIDIEndpointRef dst = LookupMIDIDest(name);
        if (!dst)
            return {};

        std::unique_ptr<IMIDIInOut> ret = std::make_unique<MIDIInOut>(false, std::move(receiver));
        if (!ret)
            return {};

        char mname[256];
        snprintf(mname, 256, "Boo MIDI Real In %u\n", m_midiInCounter++);
        CFStringRef midiName = CFStringCreateWithCStringNoCopy(nullptr, mname, kCFStringEncodingUTF8, kCFAllocatorNull);
        if (MIDIInputPortCreate(m_midiClient, midiName, MIDIReadProc(MIDIReceiveProc),
                                static_cast<IMIDIReceiver*>(ret.get()),
                                &static_cast<MIDIInOut&>(*ret).m_midiPortIn))
            ret.reset();
        else
            MIDIPortConnectSource(static_cast<MIDIInOut&>(*ret).m_midiPortIn, src, nullptr);
        CFRelease(midiName);

        if (!ret)
            return {};

        snprintf(mname, 256, "Boo MIDI Real Out %u\n", m_midiOutCounter++);
        midiName = CFStringCreateWithCStringNoCopy(nullptr, mname, kCFStringEncodingUTF8, kCFAllocatorNull);
        if (MIDIOutputPortCreate(m_midiClient, midiName, &static_cast<MIDIInOut&>(*ret).m_midiPortOut))
            ret.reset();
        else
            static_cast<MIDIInOut&>(*ret).m_midiOut = dst;
        CFRelease(midiName);

        return ret;
    }

    AQSAudioVoiceEngine()
    {
        m_mixInfo.m_channels = _getAvailableSet();
        unsigned chCount = ChannelCount(m_mixInfo.m_channels);

        AudioStreamBasicDescription desc = {};
        desc.mSampleRate = 96000;
        desc.mFormatID = kAudioFormatLinearPCM;
        desc.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger;
        desc.mBytesPerPacket = chCount * 4;
        desc.mFramesPerPacket = 1;
        desc.mBytesPerFrame = chCount * 4;
        desc.mChannelsPerFrame = chCount;
        desc.mBitsPerChannel = 32;

        OSStatus err;
        if ((err = AudioQueueNewOutput(&desc, AudioQueueOutputCallback(Callback),
                                       this, nullptr, nullptr, 0, &m_queue)))
        {
            Log.report(logvisor::Fatal, "unable to create output audio queue");
            return;
        }

        m_mixInfo.m_sampleRate = 96000.0;
        m_mixInfo.m_sampleFormat = SOXR_INT32_I;
        m_mixInfo.m_bitsPerSample = 32;
        m_5msFrames = 96000 * 5 / 1000;

        ChannelMap& chMapOut = m_mixInfo.m_channelMap;
        if (chCount > 2)
        {
            AudioChannelLayout layout;
            UInt32 layoutSz = sizeof(layout);
            if (AudioQueueGetProperty(m_queue, kAudioQueueProperty_ChannelLayout, &layout, &layoutSz))
            {
                Log.report(logvisor::Fatal, "unable to get channel layout from audio queue");
                return;
            }

            switch (layout.mChannelLayoutTag)
            {
            case kAudioChannelLayoutTag_UseChannelDescriptions:
                chMapOut.m_channelCount = layout.mNumberChannelDescriptions;
                for (int i=0 ; i<layout.mNumberChannelDescriptions ; ++i)
                {
                    AudioChannel ch = AQSChannelToBooChannel(layout.mChannelDescriptions[i].mChannelLabel);
                    chMapOut.m_channels[i] = ch;
                }
                break;
            case kAudioChannelLayoutTag_UseChannelBitmap:
                if ((layout.mChannelBitmap & kAudioChannelBit_Left) != 0)
                    chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::FrontLeft;
                if ((layout.mChannelBitmap & kAudioChannelBit_Right) != 0)
                    chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::FrontRight;
                if ((layout.mChannelBitmap & kAudioChannelBit_Center) != 0)
                    chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::FrontCenter;
                if ((layout.mChannelBitmap & kAudioChannelBit_LFEScreen) != 0)
                    chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::LFE;
                if ((layout.mChannelBitmap & kAudioChannelBit_LeftSurround) != 0)
                    chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::RearLeft;
                if ((layout.mChannelBitmap & kAudioChannelBit_RightSurround) != 0)
                    chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::RearRight;
                if ((layout.mChannelBitmap & kAudioChannelBit_LeftSurroundDirect) != 0)
                    chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::SideLeft;
                if ((layout.mChannelBitmap & kAudioChannelBit_RightSurroundDirect) != 0)
                    chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::SideRight;
                break;
            case kAudioChannelLayoutTag_Stereo:
            case kAudioChannelLayoutTag_StereoHeadphones:
                chMapOut.m_channelCount = 2;
                chMapOut.m_channels[0] = AudioChannel::FrontLeft;
                chMapOut.m_channels[1] = AudioChannel::FrontRight;
                break;
            case kAudioChannelLayoutTag_Quadraphonic:
                chMapOut.m_channelCount = 4;
                chMapOut.m_channels[0] = AudioChannel::FrontLeft;
                chMapOut.m_channels[1] = AudioChannel::FrontRight;
                chMapOut.m_channels[2] = AudioChannel::RearLeft;
                chMapOut.m_channels[3] = AudioChannel::RearRight;
                break;
            case kAudioChannelLayoutTag_Pentagonal:
                chMapOut.m_channelCount = 5;
                chMapOut.m_channels[0] = AudioChannel::FrontLeft;
                chMapOut.m_channels[1] = AudioChannel::FrontRight;
                chMapOut.m_channels[2] = AudioChannel::RearLeft;
                chMapOut.m_channels[3] = AudioChannel::RearRight;
                chMapOut.m_channels[4] = AudioChannel::FrontCenter;
                break;
            default:
                Log.report(logvisor::Fatal,
                           "unknown channel layout %u; using stereo",
                           layout.mChannelLayoutTag);
                chMapOut.m_channelCount = 2;
                chMapOut.m_channels[0] = AudioChannel::FrontLeft;
                chMapOut.m_channels[1] = AudioChannel::FrontRight;
                break;
            }
        }
        else
        {
            chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::FrontLeft;
            chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::FrontRight;
        }

        while (chMapOut.m_channelCount < chCount)
            chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::Unknown;

        m_mixInfo.m_periodFrames = 2400;
        for (int i=0 ; i<3 ; ++i)
            if (AudioQueueAllocateBuffer(m_queue, m_mixInfo.m_periodFrames * chCount * 4, &m_buffers[i]))
            {
                Log.report(logvisor::Fatal, "unable to create audio queue buffer");
                AudioQueueDispose(m_queue, false);
                m_queue = nullptr;
                return;
            }

        m_frameBytes = m_mixInfo.m_periodFrames * m_mixInfo.m_channelMap.m_channelCount * 4;

        for (unsigned i=0 ; i<3 ; ++i)
        {
            _pumpAndMixVoices(m_mixInfo.m_periodFrames, reinterpret_cast<int32_t*>(m_buffers[i]->mAudioData));
            m_buffers[i]->mAudioDataByteSize = m_frameBytes;
            AudioQueueEnqueueBuffer(m_queue, m_buffers[i], 0, nullptr);
        }
        AudioQueuePrime(m_queue, 0, nullptr);
        AudioQueueStart(m_queue, nullptr);

        /* Also create shared MIDI client */
        MIDIClientCreate(CFSTR("Boo MIDI"), nullptr, nullptr, &m_midiClient);
    }

    ~AQSAudioVoiceEngine()
    {
        AudioQueueDispose(m_queue, false);
        MIDIClientDispose(m_midiClient);
    }

    void pumpAndMixVoices()
    {
        std::unique_lock<std::mutex> lk(m_engineMutex);
        if (m_cbWaiting)
        {
            m_engineEnterCv.notify_one();
            m_engineLeaveCv.wait(lk);
        }
    }
};

std::unique_ptr<IAudioVoiceEngine> NewAudioVoiceEngine()
{
    std::unique_ptr<IAudioVoiceEngine> ret = std::make_unique<AQSAudioVoiceEngine>();
    if (!static_cast<AQSAudioVoiceEngine&>(*ret).m_queue)
        return {};
    return ret;
}

}
