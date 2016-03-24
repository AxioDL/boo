#include "AudioVoiceEngine.hpp"
#include "logvisor/logvisor.hpp"

#include <AudioToolbox/AudioToolbox.h>

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

    std::mutex m_engineMutex;
    std::condition_variable m_engineCv;

    static void Callback(AQSAudioVoiceEngine* engine, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer)
    {
        std::unique_lock<std::mutex> lk(engine->m_engineMutex);
        engine->m_engineCv.wait(lk);

        engine->_pumpAndMixVoices(engine->m_mixInfo.m_periodFrames, reinterpret_cast<int32_t*>(inBuffer->mAudioData));
        inBuffer->mAudioDataByteSize = engine->m_frameBytes;
        AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, nullptr);
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
    }

    ~AQSAudioVoiceEngine()
    {
        AudioQueueDispose(m_queue, false);
    }

    void pumpAndMixVoices()
    {
        std::unique_lock<std::mutex> lk(m_engineMutex);
        m_engineCv.notify_one();
        lk.unlock();
        lk.lock();
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
