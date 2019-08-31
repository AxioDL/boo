#include "lib/audiodev/AudioVoiceEngine.hpp"

#include "boo/IApplication.hpp"
#include "lib/CFPointer.hpp"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreMIDI/CoreMIDI.h>
#include <CoreAudio/HostTime.h>

#include <logvisor/logvisor.hpp>

namespace boo {
static logvisor::Module Log("boo::AQS");

#define AQS_NUM_BUFFERS 24

static AudioChannel AQSChannelToBooChannel(AudioChannelLabel ch) {
  switch (ch) {
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

struct AQSAudioVoiceEngine : BaseAudioVoiceEngine {
  CFPointer<CFStringRef> m_runLoopMode;
  CFPointer<CFStringRef> m_devName;

  AudioQueueRef m_queue = nullptr;
  AudioQueueBufferRef m_buffers[AQS_NUM_BUFFERS];
  size_t m_frameBytes;

  MIDIClientRef m_midiClient = 0;

  bool m_cbRunning = true;
  bool m_needsRebuild = false;

  static void Callback(AQSAudioVoiceEngine* engine, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer) {
    if (!engine->m_cbRunning)
      return;
    engine->_pumpAndMixVoices(engine->m_mixInfo.m_periodFrames, reinterpret_cast<float*>(inBuffer->mAudioData));
    inBuffer->mAudioDataByteSize = engine->m_frameBytes;
    AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, nullptr);
  }

  static void DummyCallback(AQSAudioVoiceEngine* engine, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer) {}

  std::pair<AudioChannelSet, Float64> _getAvailableSetAndRate() {
    AudioObjectPropertyAddress propertyAddress;
    UInt32 argSize;
    int numStreams;
    std::vector<AudioStreamID> streamIDs;

    CFStringRef devName = m_devName.get();
    AudioObjectID devId;
    propertyAddress.mSelector = kAudioHardwarePropertyTranslateUIDToDevice;
    propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
    propertyAddress.mElement = kAudioObjectPropertyElementMaster;
    argSize = sizeof(devId);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, sizeof(devName), &devName, &argSize,
                                   &devId) != noErr) {
      Log.report(logvisor::Error, fmt("unable to resolve audio device UID {}, using default"),
                 CFStringGetCStringPtr(devName, kCFStringEncodingUTF8));
      argSize = sizeof(devId);
      propertyAddress.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
      if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &argSize, &devId) == noErr) {
        argSize = sizeof(CFStringRef);
        AudioObjectPropertyAddress deviceAddress;
        deviceAddress.mSelector = kAudioDevicePropertyDeviceUID;
        AudioObjectGetPropertyData(devId, &deviceAddress, 0, nullptr, &argSize, &m_devName);
      } else {
        Log.report(logvisor::Fatal, fmt("unable determine default audio device"));
        return {AudioChannelSet::Unknown, 48000.0};
      }
    }

    propertyAddress.mSelector = kAudioDevicePropertyStreams;
    if (AudioObjectGetPropertyDataSize(devId, &propertyAddress, 0, nullptr, &argSize) == noErr) {
      numStreams = argSize / sizeof(AudioStreamID);
      streamIDs.resize(numStreams);

      if (AudioObjectGetPropertyData(devId, &propertyAddress, 0, nullptr, &argSize, &streamIDs[0]) == noErr) {
        propertyAddress.mSelector = kAudioStreamPropertyDirection;
        for (int stm = 0; stm < numStreams; stm++) {
          UInt32 streamDir;
          argSize = sizeof(streamDir);
          if (AudioObjectGetPropertyData(streamIDs[stm], &propertyAddress, 0, nullptr, &argSize, &streamDir) == noErr) {
            if (streamDir == 0) {
              propertyAddress.mSelector = kAudioStreamPropertyPhysicalFormat;
              AudioStreamBasicDescription asbd;
              argSize = sizeof(asbd);
              if (AudioObjectGetPropertyData(streamIDs[stm], &propertyAddress, 0, nullptr, &argSize, &asbd) == noErr) {
                switch (asbd.mChannelsPerFrame) {
                case 2:
                  return {AudioChannelSet::Stereo, asbd.mSampleRate};
                case 4:
                  return {AudioChannelSet::Quad, asbd.mSampleRate};
                case 6:
                  return {AudioChannelSet::Surround51, asbd.mSampleRate};
                case 8:
                  return {AudioChannelSet::Surround71, asbd.mSampleRate};
                default:
                  break;
                }
                if (asbd.mChannelsPerFrame > 8)
                  return {AudioChannelSet::Surround71, asbd.mSampleRate};
              }
              break;
            }
          }
        }
      }
    }

    return {AudioChannelSet::Unknown, 48000.0};
  }

  std::string getCurrentAudioOutput() const override { return CFStringGetCStringPtr(m_devName.get(), kCFStringEncodingUTF8); }

  bool setCurrentAudioOutput(const char* name) override {
    m_devName = CFPointer<CFStringRef>::adopt(CFStringCreateWithCString(nullptr, name, kCFStringEncodingUTF8));
    _rebuildAudioQueue();
    return true;
  }

  /*
   * https://stackoverflow.com/questions/1983984/how-to-get-audio-device-uid-to-pass-into-nssounds-setplaybackdeviceidentifier
   */
  std::vector<std::pair<std::string, std::string>> enumerateAudioOutputs() const {
    std::vector<std::pair<std::string, std::string>> ret;

    AudioObjectPropertyAddress propertyAddress;
    std::vector<AudioObjectID> deviceIDs;
    UInt32 propertySize;
    int numDevices;

    std::vector<AudioStreamID> streamIDs;
    int numStreams;

    propertyAddress.mSelector = kAudioHardwarePropertyDevices;
    propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
    propertyAddress.mElement = kAudioObjectPropertyElementMaster;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &propertySize) == noErr) {
      numDevices = propertySize / sizeof(AudioDeviceID);
      ret.reserve(numDevices);
      deviceIDs.resize(numDevices);

      if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &propertySize,
                                     &deviceIDs[0]) == noErr) {
        char deviceName[64];

        for (int idx = 0; idx < numDevices; idx++) {
          propertyAddress.mSelector = kAudioDevicePropertyStreams;
          if (AudioObjectGetPropertyDataSize(deviceIDs[idx], &propertyAddress, 0, nullptr, &propertySize) == noErr) {
            numStreams = propertySize / sizeof(AudioStreamID);
            streamIDs.resize(numStreams);

            if (AudioObjectGetPropertyData(deviceIDs[idx], &propertyAddress, 0, nullptr, &propertySize, &streamIDs[0]) ==
                noErr) {
              propertyAddress.mSelector = kAudioStreamPropertyDirection;
              bool foundOutput = false;
              for (int stm = 0; stm < numStreams; stm++) {
                UInt32 streamDir;
                propertySize = sizeof(streamDir);
                if (AudioObjectGetPropertyData(streamIDs[stm], &propertyAddress, 0, nullptr, &propertySize, &streamDir) ==
                    noErr) {
                  if (streamDir == 0) {
                    foundOutput = true;
                    break;
                  }
                }
              }
              if (!foundOutput)
                continue;
            }
          }

          propertySize = sizeof(deviceName);
          propertyAddress.mSelector = kAudioDevicePropertyDeviceName;
          if (AudioObjectGetPropertyData(deviceIDs[idx], &propertyAddress, 0, nullptr, &propertySize, deviceName) ==
              noErr) {
            CFPointer<CFStringRef> uidString;

            propertySize = sizeof(CFStringRef);
            propertyAddress.mSelector = kAudioDevicePropertyDeviceUID;
            if (AudioObjectGetPropertyData(deviceIDs[idx], &propertyAddress, 0, nullptr, &propertySize, &uidString) ==
                noErr) {
              ret.emplace_back(CFStringGetCStringPtr(uidString.get(), kCFStringEncodingUTF8), deviceName);
            }
          }
        }
      }
    }

    return ret;
  }

  std::vector<std::pair<std::string, std::string>> enumerateMIDIInputs() const {
    if (!m_midiClient)
      return {};

    std::vector<std::pair<std::string, std::string>> ret;

    ItemCount numDevices = MIDIGetNumberOfDevices();
    ret.reserve(numDevices);
    for (int i = int(numDevices) - 1; i >= 0; --i) {
      MIDIDeviceRef dev = MIDIGetDevice(i);
      if (!dev)
        continue;

      bool isInput = false;
      ItemCount numEnt = MIDIDeviceGetNumberOfEntities(dev);
      for (ItemCount j = 0; j < numEnt; ++j) {
        MIDIEntityRef ent = MIDIDeviceGetEntity(dev, j);
        if (ent) {
          ItemCount numSrc = MIDIEntityGetNumberOfSources(ent);
          if (numSrc) {
            isInput = true;
            break;
          }
        }
      }
      if (!isInput)
        continue;

      SInt32 idNum;
      if (MIDIObjectGetIntegerProperty(dev, kMIDIPropertyUniqueID, &idNum))
        continue;

      CFPointer<CFStringRef> namestr;
      const char* nameCstr;
      if (MIDIObjectGetStringProperty(dev, kMIDIPropertyName, &namestr))
        continue;

      if (!(nameCstr = CFStringGetCStringPtr(namestr.get(), kCFStringEncodingUTF8)))
        continue;

      ret.push_back(std::make_pair(fmt::format(fmt("{:08X}"), idNum), std::string(nameCstr)));
    }

    return ret;
  }

  bool supportsVirtualMIDIIn() const { return true; }

  static MIDIDeviceRef LookupMIDIDevice(const char* name) {
    ItemCount numDevices = MIDIGetNumberOfDevices();
    for (ItemCount i = 0; i < numDevices; ++i) {
      MIDIDeviceRef dev = MIDIGetDevice(i);
      if (!dev)
        continue;

      SInt32 idNum;
      if (MIDIObjectGetIntegerProperty(dev, kMIDIPropertyUniqueID, &idNum))
        continue;

      if (fmt::format(fmt("{:08X}"), idNum) != name)
        continue;

      return dev;
    }

    return {};
  }

  static MIDIEndpointRef LookupMIDISource(const char* name) {
    MIDIDeviceRef dev = LookupMIDIDevice(name);
    if (!dev)
      return {};

    ItemCount numEnt = MIDIDeviceGetNumberOfEntities(dev);
    for (ItemCount i = 0; i < numEnt; ++i) {
      MIDIEntityRef ent = MIDIDeviceGetEntity(dev, i);
      if (ent) {
        ItemCount numSrc = MIDIEntityGetNumberOfSources(ent);
        for (ItemCount s = 0; s < numSrc; ++s) {
          MIDIEndpointRef src = MIDIEntityGetSource(ent, s);
          if (src)
            return src;
        }
      }
    }

    return {};
  }

  static MIDIEndpointRef LookupMIDIDest(const char* name) {
    MIDIDeviceRef dev = LookupMIDIDevice(name);
    if (!dev)
      return {};

    ItemCount numEnt = MIDIDeviceGetNumberOfEntities(dev);
    for (ItemCount i = 0; i < numEnt; ++i) {
      MIDIEntityRef ent = MIDIDeviceGetEntity(dev, i);
      if (ent) {
        ItemCount numDest = MIDIEntityGetNumberOfDestinations(ent);
        for (ItemCount d = 0; d < numDest; ++d) {
          MIDIEndpointRef dst = MIDIEntityGetDestination(ent, d);
          if (dst)
            return dst;
        }
      }
    }

    return {};
  }

  static void MIDIReceiveProc(const MIDIPacketList* pktlist, IMIDIReceiver* readProcRefCon, void*) {
    const MIDIPacket* packet = &pktlist->packet[0];
    for (int i = 0; i < pktlist->numPackets; ++i) {
      std::vector<uint8_t> bytes(std::cbegin(packet->data), std::cbegin(packet->data) + packet->length);
      readProcRefCon->m_receiver(std::move(bytes), AudioConvertHostTimeToNanos(packet->timeStamp) / 1.0e9);
      packet = MIDIPacketNext(packet);
    }
  }

  struct MIDIIn : public IMIDIIn {
    MIDIEndpointRef m_midi = 0;
    MIDIPortRef m_midiPort = 0;

    MIDIIn(AQSAudioVoiceEngine* parent, bool virt, ReceiveFunctor&& receiver)
    : IMIDIIn(parent, virt, std::move(receiver)) {}

    ~MIDIIn() override {
      if (m_midi)
        MIDIEndpointDispose(m_midi);
      if (m_midiPort)
        MIDIPortDispose(m_midiPort);
    }

    std::string description() const override {
      CFPointer<CFStringRef> namestr;
      const char* nameCstr;
      if (MIDIObjectGetStringProperty(m_midi, kMIDIPropertyName, &namestr))
        return {};

      if (!(nameCstr = CFStringGetCStringPtr(namestr.get(), kCFStringEncodingUTF8)))
        return {};

      return nameCstr;
    }
  };

  struct MIDIOut : public IMIDIOut {
    MIDIEndpointRef m_midi = 0;
    MIDIPortRef m_midiPort = 0;

    MIDIOut(AQSAudioVoiceEngine* parent, bool virt) : IMIDIOut(parent, virt) {}

    ~MIDIOut() override {
      if (m_midi)
        MIDIEndpointDispose(m_midi);
      if (m_midiPort)
        MIDIPortDispose(m_midiPort);
    }

    std::string description() const override {
      CFPointer<CFStringRef> namestr;
      const char* nameCstr;
      if (MIDIObjectGetStringProperty(m_midi, kMIDIPropertyName, &namestr))
        return {};

      if (!(nameCstr = CFStringGetCStringPtr(namestr.get(), kCFStringEncodingUTF8)))
        return {};

      return nameCstr;
    }

    size_t send(const void* buf, size_t len) const override {
      union {
        MIDIPacketList head;
        Byte storage[512];
      } list;
      MIDIPacket* curPacket = MIDIPacketListInit(&list.head);
      if (MIDIPacketListAdd(&list.head, sizeof(list), curPacket, AudioGetCurrentHostTime(), len,
                            reinterpret_cast<const Byte*>(buf))) {
        if (m_midiPort)
          MIDISend(m_midiPort, m_midi, &list.head);
        else
          MIDIReceived(m_midi, &list.head);
        return len;
      }
      return 0;
    }
  };

  struct MIDIInOut : public IMIDIInOut {
    MIDIEndpointRef m_midiIn = 0;
    MIDIPortRef m_midiPortIn = 0;
    MIDIEndpointRef m_midiOut = 0;
    MIDIPortRef m_midiPortOut = 0;

    MIDIInOut(AQSAudioVoiceEngine* parent, bool virt, ReceiveFunctor&& receiver)
    : IMIDIInOut(parent, virt, std::move(receiver)) {}

    ~MIDIInOut() override {
      if (m_midiIn)
        MIDIEndpointDispose(m_midiIn);
      if (m_midiPortIn)
        MIDIPortDispose(m_midiPortIn);
      if (m_midiOut)
        MIDIEndpointDispose(m_midiOut);
      if (m_midiPortOut)
        MIDIPortDispose(m_midiPortOut);
    }

    std::string description() const override {
      CFPointer<CFStringRef> namestr;
      const char* nameCstr;
      if (MIDIObjectGetStringProperty(m_midiIn, kMIDIPropertyName, &namestr))
        return {};

      if (!(nameCstr = CFStringGetCStringPtr(namestr.get(), kCFStringEncodingUTF8)))
        return {};

      return nameCstr;
    }

    size_t send(const void* buf, size_t len) const override {
      union {
        MIDIPacketList head;
        Byte storage[512];
      } list;
      MIDIPacket* curPacket = MIDIPacketListInit(&list.head);
      if (MIDIPacketListAdd(&list.head, sizeof(list), curPacket, AudioGetCurrentHostTime(), len,
                            reinterpret_cast<const Byte*>(buf))) {
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

  std::unique_ptr<IMIDIIn> newVirtualMIDIIn(ReceiveFunctor&& receiver) override {
    if (!m_midiClient)
      return {};

    std::unique_ptr<IMIDIIn> ret = std::make_unique<MIDIIn>(this, true, std::move(receiver));
    if (!ret)
      return {};

    std::string name;
    auto appName = APP->getFriendlyName();
    if (!m_midiInCounter)
      name = fmt::format(fmt("{} MIDI-In"), appName);
    else
      name = fmt::format(fmt("{} MIDI-In {}"), appName, m_midiInCounter);
    m_midiInCounter++;
    CFPointer<CFStringRef> midiName = CFPointer<CFStringRef>::adopt(
        CFStringCreateWithCStringNoCopy(nullptr, name.c_str(), kCFStringEncodingUTF8, kCFAllocatorNull));
    OSStatus stat;
    if ((stat = MIDIDestinationCreate(m_midiClient, midiName.get(), MIDIReadProc(MIDIReceiveProc),
                                      static_cast<IMIDIReceiver*>(ret.get()), &static_cast<MIDIIn&>(*ret).m_midi)))
      ret.reset();

    return ret;
  }

  std::unique_ptr<IMIDIOut> newVirtualMIDIOut() override {
    if (!m_midiClient)
      return {};

    std::unique_ptr<IMIDIOut> ret = std::make_unique<MIDIOut>(this, true);
    if (!ret)
      return {};

    std::string name;
    auto appName = APP->getFriendlyName();
    if (!m_midiOutCounter)
      name = fmt::format(fmt("{} MIDI-Out"), appName);
    else
      name = fmt::format(fmt("{} MIDI-Out {}"), appName, m_midiOutCounter);
    m_midiOutCounter++;
    CFPointer<CFStringRef> midiName = CFPointer<CFStringRef>::adopt(
        CFStringCreateWithCStringNoCopy(nullptr, name.c_str(), kCFStringEncodingUTF8, kCFAllocatorNull));
    if (MIDISourceCreate(m_midiClient, midiName.get(), &static_cast<MIDIOut&>(*ret).m_midi))
      ret.reset();

    return ret;
  }

  std::unique_ptr<IMIDIInOut> newVirtualMIDIInOut(ReceiveFunctor&& receiver) override {
    if (!m_midiClient)
      return {};

    std::unique_ptr<IMIDIInOut> ret = std::make_unique<MIDIInOut>(this, true, std::move(receiver));
    if (!ret)
      return {};

    std::string name;
    auto appName = APP->getFriendlyName();
    if (!m_midiInCounter)
      name = fmt::format(fmt("{} MIDI-In"), appName);
    else
      name = fmt::format(fmt("{} MIDI-In {}"), appName, m_midiInCounter);
    m_midiInCounter++;
    CFPointer<CFStringRef> midiName = CFPointer<CFStringRef>::adopt(
        CFStringCreateWithCStringNoCopy(nullptr, name.c_str(), kCFStringEncodingUTF8, kCFAllocatorNull));
    if (MIDIDestinationCreate(m_midiClient, midiName.get(), MIDIReadProc(MIDIReceiveProc),
                              static_cast<IMIDIReceiver*>(ret.get()), &static_cast<MIDIInOut&>(*ret).m_midiIn))
      ret.reset();

    if (!ret)
      return {};

    if (!m_midiOutCounter)
      name = fmt::format(fmt("{} MIDI-Out"), appName);
    else
      name = fmt::format(fmt("{} MIDI-Out {}"), appName, m_midiOutCounter);
    m_midiOutCounter++;
    midiName = CFPointer<CFStringRef>::adopt(
        CFStringCreateWithCStringNoCopy(nullptr, name.c_str(), kCFStringEncodingUTF8, kCFAllocatorNull));
    if (MIDISourceCreate(m_midiClient, midiName.get(), &static_cast<MIDIInOut&>(*ret).m_midiOut))
      ret.reset();

    return ret;
  }

  std::unique_ptr<IMIDIIn> newRealMIDIIn(const char* name, ReceiveFunctor&& receiver) override {
    if (!m_midiClient)
      return {};

    MIDIEndpointRef src = LookupMIDISource(name);
    if (!src)
      return {};

    std::unique_ptr<IMIDIIn> ret = std::make_unique<MIDIIn>(this, false, std::move(receiver));
    if (!ret)
      return {};

    std::string mname = fmt::format(fmt("Boo MIDI Real In {}"), m_midiInCounter++);
    CFPointer<CFStringRef> midiName = CFPointer<CFStringRef>::adopt(
        CFStringCreateWithCStringNoCopy(nullptr, mname.c_str(), kCFStringEncodingUTF8, kCFAllocatorNull));
    if (MIDIInputPortCreate(m_midiClient, midiName.get(), MIDIReadProc(MIDIReceiveProc),
                            static_cast<IMIDIReceiver*>(ret.get()), &static_cast<MIDIIn&>(*ret).m_midiPort))
      ret.reset();
    else
      MIDIPortConnectSource(static_cast<MIDIIn&>(*ret).m_midiPort, src, nullptr);

    return ret;
  }

  std::unique_ptr<IMIDIOut> newRealMIDIOut(const char* name) override {
    if (!m_midiClient)
      return {};

    MIDIEndpointRef dst = LookupMIDIDest(name);
    if (!dst)
      return {};

    std::unique_ptr<IMIDIOut> ret = std::make_unique<MIDIOut>(this, false);
    if (!ret)
      return {};

    std::string mname = fmt::format(fmt("Boo MIDI Real Out {}"), m_midiOutCounter++);
    CFPointer<CFStringRef> midiName = CFPointer<CFStringRef>::adopt(
        CFStringCreateWithCStringNoCopy(nullptr, mname.c_str(), kCFStringEncodingUTF8, kCFAllocatorNull));
    if (MIDIOutputPortCreate(m_midiClient, midiName.get(), &static_cast<MIDIOut&>(*ret).m_midiPort))
      ret.reset();
    else
      static_cast<MIDIOut&>(*ret).m_midi = dst;

    return ret;
  }

  std::unique_ptr<IMIDIInOut> newRealMIDIInOut(const char* name, ReceiveFunctor&& receiver) override {
    if (!m_midiClient)
      return {};

    MIDIEndpointRef src = LookupMIDISource(name);
    if (!src)
      return {};

    MIDIEndpointRef dst = LookupMIDIDest(name);
    if (!dst)
      return {};

    std::unique_ptr<IMIDIInOut> ret = std::make_unique<MIDIInOut>(this, false, std::move(receiver));
    if (!ret)
      return {};

    std::string mname = fmt::format(fmt("Boo MIDI Real In {}"), m_midiInCounter++);
    CFPointer<CFStringRef> midiName = CFPointer<CFStringRef>::adopt(
        CFStringCreateWithCStringNoCopy(nullptr, mname.c_str(), kCFStringEncodingUTF8, kCFAllocatorNull));
    if (MIDIInputPortCreate(m_midiClient, midiName.get(), MIDIReadProc(MIDIReceiveProc),
                            static_cast<IMIDIReceiver*>(ret.get()), &static_cast<MIDIInOut&>(*ret).m_midiPortIn))
      ret.reset();
    else
      MIDIPortConnectSource(static_cast<MIDIInOut&>(*ret).m_midiPortIn, src, nullptr);

    if (!ret)
      return {};

    mname = fmt::format(fmt("Boo MIDI Real Out {}"), m_midiOutCounter++);
    midiName = CFPointer<CFStringRef>::adopt(
        CFStringCreateWithCStringNoCopy(nullptr, mname.c_str(), kCFStringEncodingUTF8, kCFAllocatorNull));
    if (MIDIOutputPortCreate(m_midiClient, midiName.get(), &static_cast<MIDIInOut&>(*ret).m_midiPortOut))
      ret.reset();
    else
      static_cast<MIDIInOut&>(*ret).m_midiOut = dst;

    return ret;
  }

  bool useMIDILock() const override { return true; }

  static void SampleRateChanged(AQSAudioVoiceEngine* engine, AudioQueueRef inAQ, AudioQueuePropertyID inID) {
    engine->m_needsRebuild = true;
  }

  void _rebuildAudioQueue() {
    if (m_queue) {
      m_cbRunning = false;
      AudioQueueDispose(m_queue, true);
      m_cbRunning = true;
      m_queue = nullptr;
    }

    auto setAndRate = _getAvailableSetAndRate();
    m_mixInfo.m_channels = setAndRate.first;
    unsigned chCount = ChannelCount(m_mixInfo.m_channels);

    AudioStreamBasicDescription desc = {};
    desc.mSampleRate = setAndRate.second;
    desc.mFormatID = kAudioFormatLinearPCM;
    desc.mFormatFlags = kLinearPCMFormatFlagIsFloat;
    desc.mBytesPerPacket = chCount * 4;
    desc.mFramesPerPacket = 1;
    desc.mBytesPerFrame = chCount * 4;
    desc.mChannelsPerFrame = chCount;
    desc.mBitsPerChannel = 32;

    OSStatus err;
    if ((err = AudioQueueNewOutput(&desc, AudioQueueOutputCallback(Callback), this, CFRunLoopGetCurrent(),
                                   m_runLoopMode.get(), 0, &m_queue))) {
      Log.report(logvisor::Fatal, fmt("unable to create output audio queue"));
      return;
    }

    CFStringRef devName = m_devName.get();
    if ((err = AudioQueueSetProperty(m_queue, kAudioQueueProperty_CurrentDevice, &devName, sizeof(devName)))) {
      Log.report(logvisor::Fatal, fmt("unable to set current device into audio queue"));
      return;
    }

    AudioQueueAddPropertyListener(m_queue, kAudioQueueDeviceProperty_SampleRate,
                                  AudioQueuePropertyListenerProc(SampleRateChanged), this);

    m_mixInfo.m_sampleRate = desc.mSampleRate;
    m_mixInfo.m_sampleFormat = SOXR_FLOAT32_I;
    m_mixInfo.m_bitsPerSample = 32;
    m_5msFrames = desc.mSampleRate * 5 / 1000;

    ChannelMap& chMapOut = m_mixInfo.m_channelMap;
    chMapOut.m_channelCount = 0;
    if (chCount > 2) {
      AudioChannelLayout layout;
      UInt32 layoutSz = sizeof(layout);
      if (AudioQueueGetProperty(m_queue, kAudioQueueProperty_ChannelLayout, &layout, &layoutSz)) {
        Log.report(logvisor::Warning, fmt("unable to get channel layout from audio queue; using count's default"));
        switch (m_mixInfo.m_channels) {
        case AudioChannelSet::Stereo:
        default:
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::FrontLeft;
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::FrontRight;
          break;
        case AudioChannelSet::Quad:
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::FrontLeft;
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::FrontRight;
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::RearLeft;
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::RearRight;
          break;
        case AudioChannelSet::Surround51:
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::FrontLeft;
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::FrontRight;
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::FrontCenter;
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::LFE;
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::RearLeft;
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::RearRight;
          break;
        case AudioChannelSet::Surround71:
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::FrontLeft;
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::FrontRight;
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::FrontCenter;
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::LFE;
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::SideLeft;
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::SideRight;
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::RearLeft;
          chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::RearRight;
          break;
        }
      } else {
        switch (layout.mChannelLayoutTag) {
        case kAudioChannelLayoutTag_UseChannelDescriptions:
          chMapOut.m_channelCount = layout.mNumberChannelDescriptions;
          for (int i = 0; i < layout.mNumberChannelDescriptions; ++i) {
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
          Log.report(logvisor::Warning, fmt("unknown channel layout {}; using stereo"), layout.mChannelLayoutTag);
          chMapOut.m_channelCount = 2;
          chMapOut.m_channels[0] = AudioChannel::FrontLeft;
          chMapOut.m_channels[1] = AudioChannel::FrontRight;
          break;
        }
      }
    } else {
      chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::FrontLeft;
      chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::FrontRight;
    }

    while (chMapOut.m_channelCount < chCount)
      chMapOut.m_channels[chMapOut.m_channelCount++] = AudioChannel::Unknown;

    m_mixInfo.m_periodFrames = m_5msFrames;
    for (int i = 0; i < AQS_NUM_BUFFERS; ++i)
      if (AudioQueueAllocateBuffer(m_queue, m_mixInfo.m_periodFrames * chCount * 4, &m_buffers[i])) {
        Log.report(logvisor::Fatal, fmt("unable to create audio queue buffer"));
        AudioQueueDispose(m_queue, false);
        m_queue = nullptr;
        return;
      }

    m_frameBytes = m_mixInfo.m_periodFrames * m_mixInfo.m_channelMap.m_channelCount * 4;

    _resetSampleRate();

    for (unsigned i = 0; i < AQS_NUM_BUFFERS; ++i) {
      memset(m_buffers[i]->mAudioData, 0, m_frameBytes);
      m_buffers[i]->mAudioDataByteSize = m_frameBytes;
      AudioQueueEnqueueBuffer(m_queue, m_buffers[i], 0, nullptr);
    }
    AudioQueuePrime(m_queue, 0, nullptr);
    AudioQueueStart(m_queue, nullptr);
  }

  static OSStatus AudioDeviceChanged(AudioObjectID inObjectID, UInt32 inNumberAddresses,
                                     const AudioObjectPropertyAddress* inAddresses, AQSAudioVoiceEngine* engine) {
    AudioObjectID defaultDeviceId;
    UInt32 argSize = sizeof(defaultDeviceId);
    if (AudioObjectGetPropertyData(inObjectID, inAddresses, 0, nullptr, &argSize, &defaultDeviceId) == noErr) {
      argSize = sizeof(CFStringRef);
      AudioObjectPropertyAddress deviceAddress;
      deviceAddress.mSelector = kAudioDevicePropertyDeviceUID;
      AudioObjectGetPropertyData(defaultDeviceId, &deviceAddress, 0, nullptr, &argSize, &engine->m_devName);
    }
    engine->m_needsRebuild = true;
    return noErr;
  }

  AQSAudioVoiceEngine()
  : m_runLoopMode(CFPointer<CFStringRef>::adopt(
        CFStringCreateWithCStringNoCopy(nullptr, "BooAQSMode", kCFStringEncodingUTF8, kCFAllocatorNull))) {
    AudioObjectPropertyAddress propertyAddress;
    propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
    propertyAddress.mElement = kAudioObjectPropertyElementMaster;

    AudioObjectID defaultDeviceId;
    UInt32 argSize = sizeof(defaultDeviceId);
    propertyAddress.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &argSize, &defaultDeviceId) ==
        noErr) {
      argSize = sizeof(CFStringRef);
      AudioObjectPropertyAddress deviceAddress;
      deviceAddress.mSelector = kAudioDevicePropertyDeviceUID;
      AudioObjectGetPropertyData(defaultDeviceId, &deviceAddress, 0, nullptr, &argSize, &m_devName);
    } else {
      Log.report(logvisor::Fatal, fmt("unable determine default audio device"));
      return;
    }

    propertyAddress.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &propertyAddress,
                                   AudioObjectPropertyListenerProc(AudioDeviceChanged), this);

    _rebuildAudioQueue();

    /* Also create shared MIDI client */
    MIDIClientCreate(CFSTR("Boo MIDI"), nullptr, nullptr, &m_midiClient);
  }

  ~AQSAudioVoiceEngine() override {
    m_cbRunning = false;
    AudioQueueDispose(m_queue, true);
    if (m_midiClient)
      MIDIClientDispose(m_midiClient);
  }

  void pumpAndMixVoices() override {
    while (CFRunLoopRunInMode(m_runLoopMode.get(), 0, true) == kCFRunLoopRunHandledSource) {}
    if (m_needsRebuild) {
      _rebuildAudioQueue();
      m_needsRebuild = false;
    }
  }
};

std::unique_ptr<IAudioVoiceEngine> NewAudioVoiceEngine() {
  std::unique_ptr<IAudioVoiceEngine> ret = std::make_unique<AQSAudioVoiceEngine>();
  if (!static_cast<AQSAudioVoiceEngine&>(*ret).m_queue)
    return {};
  return ret;
}

} // namespace boo
