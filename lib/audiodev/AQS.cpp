#include "boo/audiodev/IAudioVoiceAllocator.hpp"
#include <LogVisor/LogVisor.hpp>

#include <AudioToolbox/AudioToolbox.h>

namespace boo
{
static LogVisor::LogModule Log("boo::AQS");

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

struct AQSAudioVoice : IAudioVoice
{
    ChannelMap m_map;
    IAudioVoiceCallback* m_cb;
    AudioQueueRef m_queue = nullptr;
    AudioQueueBufferRef m_buffers[3];
    size_t m_bufferFrames = 2048;
    size_t m_frameSize;

    const ChannelMap& channelMap() const {return m_map;}

    AudioQueueBufferRef m_callbackBuf = nullptr;
    unsigned m_primeBuf;
    static void Callback(void* inUserData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer)
    {
        AQSAudioVoice* voice = static_cast<AQSAudioVoice*>(inUserData);
        voice->m_callbackBuf = inBuffer;
        voice->m_cb->needsNextBuffer(voice, voice->m_bufferFrames);
        voice->m_callbackBuf = nullptr;
    }

    AQSAudioVoice(AudioChannelSet set, unsigned sampleRate, IAudioVoiceCallback* cb)
    : m_cb(cb)
    {
        unsigned chCount = ChannelCount(set);

        AudioStreamBasicDescription desc = {};
        desc.mSampleRate = sampleRate;
        desc.mFormatID = kAudioFormatLinearPCM;
        desc.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger;
        desc.mBytesPerPacket = chCount * 2;
        desc.mFramesPerPacket = 1;
        desc.mBytesPerFrame = chCount * 2;
        desc.mChannelsPerFrame = chCount;
        desc.mBitsPerChannel = 16;

        OSStatus err;
        while ((err = AudioQueueNewOutput(&desc, AudioQueueOutputCallback(Callback), this, nullptr, nullptr, 0, &m_queue)))
        {
            if (set == AudioChannelSet::Stereo)
                break;
            set = AudioChannelSet(int(set) - 1);
            chCount = ChannelCount(set);
            desc.mBytesPerPacket = chCount * 2;
            desc.mBytesPerFrame = chCount * 2;
            desc.mChannelsPerFrame = chCount;
        }
        if (err)
        {
            Log.report(LogVisor::Error, "unable to create output audio queue");
            return;
        }

        AudioChannelLayout layout;
        UInt32 layoutSz = sizeof(layout);
        if (AudioQueueGetProperty(m_queue, kAudioQueueProperty_ChannelLayout, &layout, &layoutSz))
        {
            Log.report(LogVisor::Error, "unable to get channel layout from audio queue");
            return;
        }

        switch (layout.mChannelLayoutTag)
        {
        case kAudioChannelLayoutTag_UseChannelDescriptions:
            m_map.m_channelCount = layout.mNumberChannelDescriptions;
            for (int i=0 ; i<layout.mNumberChannelDescriptions ; ++i)
            {
                AudioChannel ch = AQSChannelToBooChannel(layout.mChannelDescriptions[i].mChannelLabel);
                m_map.m_channels[i] = ch;
            }
            break;
        case kAudioChannelLayoutTag_UseChannelBitmap:
            if ((layout.mChannelBitmap & kAudioChannelBit_Left) != 0)
                m_map.m_channels[m_map.m_channelCount++] = AudioChannel::FrontLeft;
            if ((layout.mChannelBitmap & kAudioChannelBit_Right) != 0)
                m_map.m_channels[m_map.m_channelCount++] = AudioChannel::FrontRight;
            if ((layout.mChannelBitmap & kAudioChannelBit_Center) != 0)
                m_map.m_channels[m_map.m_channelCount++] = AudioChannel::FrontCenter;
            if ((layout.mChannelBitmap & kAudioChannelBit_LFEScreen) != 0)
                m_map.m_channels[m_map.m_channelCount++] = AudioChannel::LFE;
            if ((layout.mChannelBitmap & kAudioChannelBit_LeftSurround) != 0)
                m_map.m_channels[m_map.m_channelCount++] = AudioChannel::RearLeft;
            if ((layout.mChannelBitmap & kAudioChannelBit_RightSurround) != 0)
                m_map.m_channels[m_map.m_channelCount++] = AudioChannel::RearRight;
            if ((layout.mChannelBitmap & kAudioChannelBit_LeftSurroundDirect) != 0)
                m_map.m_channels[m_map.m_channelCount++] = AudioChannel::SideLeft;
            if ((layout.mChannelBitmap & kAudioChannelBit_RightSurroundDirect) != 0)
                m_map.m_channels[m_map.m_channelCount++] = AudioChannel::SideRight;
            break;
        case kAudioChannelLayoutTag_Stereo:
        case kAudioChannelLayoutTag_StereoHeadphones:
            m_map.m_channelCount = 2;
            m_map.m_channels[0] = AudioChannel::FrontLeft;
            m_map.m_channels[1] = AudioChannel::FrontRight;
            break;
        case kAudioChannelLayoutTag_Quadraphonic:
            m_map.m_channelCount = 4;
            m_map.m_channels[0] = AudioChannel::FrontLeft;
            m_map.m_channels[1] = AudioChannel::FrontRight;
            m_map.m_channels[2] = AudioChannel::RearLeft;
            m_map.m_channels[3] = AudioChannel::RearRight;
            break;
        case kAudioChannelLayoutTag_Pentagonal:
            m_map.m_channelCount = 5;
            m_map.m_channels[0] = AudioChannel::FrontLeft;
            m_map.m_channels[1] = AudioChannel::FrontRight;
            m_map.m_channels[2] = AudioChannel::RearLeft;
            m_map.m_channels[3] = AudioChannel::RearRight;
            m_map.m_channels[4] = AudioChannel::FrontCenter;
            break;
        default:
            Log.report(LogVisor::Error, "unknown channel layout %u; using stereo", layout.mChannelLayoutTag);
            m_map.m_channelCount = 2;
            m_map.m_channels[0] = AudioChannel::FrontLeft;
            m_map.m_channels[1] = AudioChannel::FrontRight;
            break;
        }

        while (m_map.m_channelCount < chCount)
            m_map.m_channels[m_map.m_channelCount++] = AudioChannel::Unknown;

        for (int i=0 ; i<3 ; ++i)
            if (AudioQueueAllocateBuffer(m_queue, m_bufferFrames * chCount * 2, &m_buffers[i]))
            {
                Log.report(LogVisor::Error, "unable to create audio queue buffer");
                AudioQueueDispose(m_queue, false);
                m_queue = nullptr;
                return;
            }

        m_frameSize = chCount * 2;

        for (unsigned i=0 ; i<3 ; ++i)
        {
            m_primeBuf = i;
            m_cb->needsNextBuffer(this, m_bufferFrames);
        }
        AudioQueuePrime(m_queue, 0, nullptr);
    }

    ~AQSAudioVoice()
    {
        AudioQueueDispose(m_queue, false);
    }

    void bufferSampleData(const int16_t* data, size_t frames)
    {
        if (m_callbackBuf)
        {
            m_callbackBuf->mAudioDataByteSize = std::min(UInt32(frames * m_frameSize), m_callbackBuf->mAudioDataBytesCapacity);
            memcpy(m_callbackBuf->mAudioData, data, m_callbackBuf->mAudioDataByteSize);
            AudioQueueEnqueueBuffer(m_queue, m_callbackBuf, 0, nullptr);
        }
        else
        {
            AudioQueueBufferRef buf = m_buffers[m_primeBuf];
            buf->mAudioDataByteSize = std::min(UInt32(frames * m_frameSize), buf->mAudioDataBytesCapacity);
            memcpy(buf->mAudioData, data, buf->mAudioDataByteSize);
            AudioQueueEnqueueBuffer(m_queue, buf, 0, nullptr);
        }
    }

    void start()
    {
        AudioQueueStart(m_queue, nullptr);
    }

    void stop()
    {
        AudioQueueStop(m_queue, false);
    }
};

struct ALSAAudioVoiceAllocator : IAudioVoiceAllocator
{
    std::unique_ptr<IAudioVoice> allocateNewVoice(AudioChannelSet layoutOut,
                                                  unsigned sampleRate,
                                                  IAudioVoiceCallback* cb)
    {
        AQSAudioVoice* newVoice = new AQSAudioVoice(layoutOut, sampleRate, cb);
        std::unique_ptr<IAudioVoice> ret(newVoice);
        if (!newVoice->m_queue)
            return {};
        return ret;
    }
};

}
