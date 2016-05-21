#include <memory>
#include <list>
#include <thread>
#include "AudioVoiceEngine.hpp"
#include "logvisor/logvisor.hpp"

#include <alsa/asoundlib.h>
#include <signal.h>

namespace boo
{
static logvisor::Module Log("boo::ALSA");

static const uint64_t StereoChans = (1 << SND_CHMAP_FL) |
                                    (1 << SND_CHMAP_FR);

static const uint64_t QuadChans = (1 << SND_CHMAP_FL) |
                                  (1 << SND_CHMAP_FR) |
                                  (1 << SND_CHMAP_RL) |
                                  (1 << SND_CHMAP_RR);

static const uint64_t S51Chans = (1 << SND_CHMAP_FL) |
                                 (1 << SND_CHMAP_FR) |
                                 (1 << SND_CHMAP_RL) |
                                 (1 << SND_CHMAP_RR) |
                                 (1 << SND_CHMAP_FC) |
                                 (1 << SND_CHMAP_LFE);

static const uint64_t S71Chans = (1 << SND_CHMAP_FL) |
                                 (1 << SND_CHMAP_FR) |
                                 (1 << SND_CHMAP_RL) |
                                 (1 << SND_CHMAP_RR) |
                                 (1 << SND_CHMAP_FC) |
                                 (1 << SND_CHMAP_LFE) |
                                 (1 << SND_CHMAP_SL) |
                                 (1 << SND_CHMAP_SR);

struct ALSAAudioVoiceEngine : BaseAudioVoiceEngine
{
    snd_pcm_t* m_pcm;
    snd_pcm_uframes_t m_bufSize;
    snd_pcm_uframes_t m_periodSize;

    std::vector<int16_t> m_final16;
    std::vector<int32_t> m_final32;
    std::vector<float> m_finalFlt;

    ~ALSAAudioVoiceEngine()
    {
        snd_pcm_drain(m_pcm);
        snd_pcm_close(m_pcm);
    }

    AudioChannelSet _getAvailableSet()
    {
        snd_pcm_chmap_query_t** chmaps = snd_pcm_query_chmaps(m_pcm);
        if (!chmaps)
            return AudioChannelSet::Stereo;

        static const std::array<AudioChannelSet, 4> testSets =
            {{AudioChannelSet::Surround71, AudioChannelSet::Surround51,
              AudioChannelSet::Quad, AudioChannelSet::Stereo}};
        for (AudioChannelSet set : testSets)
        {
            for (snd_pcm_chmap_query_t** chmap = chmaps ; *chmap != nullptr ; ++chmap)
            {
                snd_pcm_chmap_t* chm = &(*chmap)->map;
                uint64_t chBits = 0;
                for (unsigned c=0 ; c<chm->channels ; ++c)
                    chBits |= 1 << chm->pos[c];

                switch (set)
                {
                case AudioChannelSet::Stereo:
                {
                    if ((chBits & StereoChans) == StereoChans)
                    {
                        snd_pcm_free_chmaps(chmaps);
                        return AudioChannelSet::Stereo;
                    }
                    break;
                }
                case AudioChannelSet::Quad:
                {
                    if ((chBits & QuadChans) == QuadChans)
                    {
                        snd_pcm_free_chmaps(chmaps);
                        return AudioChannelSet::Quad;
                    }
                    break;
                }
                case AudioChannelSet::Surround51:
                {
                    if ((chBits & S51Chans) == S51Chans)
                    {
                        snd_pcm_free_chmaps(chmaps);
                        return AudioChannelSet::Surround51;
                    }
                    break;
                }
                case AudioChannelSet::Surround71:
                {
                    if ((chBits & S71Chans) == S71Chans)
                    {
                        snd_pcm_free_chmaps(chmaps);
                        return AudioChannelSet::Surround71;
                    }
                    break;
                }
                default: break;
                }
            }
        }

        snd_pcm_free_chmaps(chmaps);
        return AudioChannelSet::Unknown;
    }

    ALSAAudioVoiceEngine()
    {
        if (snd_pcm_open(&m_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
        {
            Log.report(logvisor::Error, "unable to allocate ALSA voice");
            return;
        }

        /* Query audio card for best supported format amd sample-rate */
        snd_pcm_hw_params_t* hwParams;
        snd_pcm_hw_params_malloc(&hwParams);
        snd_pcm_hw_params_any(m_pcm, hwParams);

        snd_pcm_format_t bestFmt;
        if (!snd_pcm_hw_params_test_format(m_pcm, hwParams, SND_PCM_FORMAT_S32))
        {
            bestFmt = SND_PCM_FORMAT_S32;
            m_mixInfo.m_sampleFormat = SOXR_INT32_I;
            m_mixInfo.m_bitsPerSample = 32;
        }
        else if (!snd_pcm_hw_params_test_format(m_pcm, hwParams, SND_PCM_FORMAT_S16))
        {
            bestFmt = SND_PCM_FORMAT_S16;
            m_mixInfo.m_sampleFormat = SOXR_INT16_I;
            m_mixInfo.m_bitsPerSample = 16;
        }
        else
        {
            snd_pcm_close(m_pcm);
            m_pcm = nullptr;
            Log.report(logvisor::Fatal, "unsupported audio formats on default ALSA device");
            return;
        }

        unsigned int bestRate;
        if (!snd_pcm_hw_params_test_rate(m_pcm, hwParams, 96000, 0))
        {
            bestRate = 96000;
            m_mixInfo.m_sampleRate = 96000.0;
            m_5msFrames = 96000 * 5 / 1000;
        }
        else if (!snd_pcm_hw_params_test_rate(m_pcm, hwParams, 48000, 0))
        {
            bestRate = 48000;
            m_mixInfo.m_sampleRate = 48000.0;
            m_5msFrames = 48000 * 5 / 1000;
        }
        else
        {
            snd_pcm_close(m_pcm);
            m_pcm = nullptr;
            Log.report(logvisor::Fatal, "unsupported audio sample rates on default ALSA device");
            return;
        }

        snd_pcm_hw_params_free(hwParams);

        /* Query audio card for channel map */
        m_mixInfo.m_channels = _getAvailableSet();

        /* Populate channel map */
        unsigned chCount = ChannelCount(m_mixInfo.m_channels);
        int err;
        while ((err = snd_pcm_set_params(m_pcm, bestFmt, SND_PCM_ACCESS_RW_INTERLEAVED,
                                         chCount, bestRate, 0, 100000)) < 0)
        {
            if (m_mixInfo.m_channels == AudioChannelSet::Stereo)
                break;
            m_mixInfo.m_channels = AudioChannelSet(int(m_mixInfo.m_channels) - 1);
            chCount = ChannelCount(m_mixInfo.m_channels);
        }
        if (err < 0)
        {
            snd_pcm_close(m_pcm);
            m_pcm = nullptr;
            Log.report(logvisor::Error, "unable to set ALSA voice params");
            return;
        }

        snd_pcm_chmap_query_t** chmaps = snd_pcm_query_chmaps(m_pcm);
        ChannelMap& chmapOut = m_mixInfo.m_channelMap;
        if (chmaps)
        {
            snd_pcm_chmap_t* foundChmap = nullptr;
            for (snd_pcm_chmap_query_t** chmap = chmaps ; *chmap != nullptr ; ++chmap)
            {
                if ((*chmap)->map.channels == chCount)
                {
                    snd_pcm_chmap_t* chm = &(*chmap)->map;
                    uint64_t chBits = 0;
                    for (unsigned c=0 ; c<chm->channels ; ++c)
                        chBits |= 1 << chm->pos[c];

                    bool good = false;
                    switch (m_mixInfo.m_channels)
                    {
                    case AudioChannelSet::Stereo:
                        if ((chBits & StereoChans) == StereoChans)
                            good = true;
                        break;
                    case AudioChannelSet::Quad:
                        if ((chBits & QuadChans) == QuadChans)
                            good = true;
                        break;
                    case AudioChannelSet::Surround51:
                        if ((chBits & S51Chans) == S51Chans)
                            good = true;
                        break;
                    case AudioChannelSet::Surround71:
                        if ((chBits & S71Chans) == S71Chans)
                            good = true;
                        break;
                    default: break;
                    }

                    if (good)
                    {
                        foundChmap = chm;
                        break;
                    }
                }
            }

            if (!foundChmap)
            {
                snd_pcm_close(m_pcm);
                m_pcm = nullptr;
                snd_pcm_free_chmaps(chmaps);
                Log.report(logvisor::Error, "unable to find matching ALSA voice chmap");
                return;
            }
            chmapOut.m_channelCount = chCount;
            for (unsigned c=0 ; c<foundChmap->channels ; ++c)
                chmapOut.m_channels[c] = AudioChannel(foundChmap->pos[c] - 3);
            snd_pcm_set_chmap(m_pcm, foundChmap);
            snd_pcm_free_chmaps(chmaps);
        }
        else
        {
            chmapOut.m_channelCount = 2;
            chmapOut.m_channels[0] = AudioChannel::FrontLeft;
            chmapOut.m_channels[1] = AudioChannel::FrontRight;
        }

        snd_pcm_get_params(m_pcm, &m_bufSize, &m_periodSize);
        snd_pcm_prepare(m_pcm);
        m_mixInfo.m_periodFrames = m_periodSize;

        /* Allocate master mix space */
        switch (m_mixInfo.m_sampleFormat)
        {
        case SOXR_INT16_I:
            m_final16.resize(m_periodSize * m_mixInfo.m_channelMap.m_channelCount);
            break;
        case SOXR_INT32_I:
            m_final32.resize(m_periodSize * m_mixInfo.m_channelMap.m_channelCount);
            break;
        case SOXR_FLOAT32_I:
            m_finalFlt.resize(m_periodSize * m_mixInfo.m_channelMap.m_channelCount);
            break;
        default:
            break;
        }
    }

    void pumpAndMixVoices()
    {
        snd_pcm_sframes_t frames = snd_pcm_avail_update(m_pcm);
        if (frames < 0)
        {
            snd_pcm_state_t st = snd_pcm_state(m_pcm);
            if (st == SND_PCM_STATE_XRUN)
            {
                snd_pcm_prepare(m_pcm);
                frames = snd_pcm_avail_update(m_pcm);
                Log.report(logvisor::Warning, "ALSA underrun %ld frames", frames);
            }
            else
                return;
        }
        if (frames < 0)
            return;

        snd_pcm_sframes_t buffers = frames / m_periodSize;
        for (snd_pcm_sframes_t b=0 ; b<buffers ; ++b)
        {
            switch (m_mixInfo.m_sampleFormat)
            {
            case SOXR_INT16_I:
                _pumpAndMixVoices(m_periodSize, m_final16.data());
                snd_pcm_writei(m_pcm, m_final16.data(), m_periodSize);
                break;
            case SOXR_INT32_I:
                _pumpAndMixVoices(m_periodSize, m_final32.data());
                snd_pcm_writei(m_pcm, m_final32.data(), m_periodSize);
                break;
            case SOXR_FLOAT32_I:
                _pumpAndMixVoices(m_periodSize, m_finalFlt.data());
                snd_pcm_writei(m_pcm, m_finalFlt.data(), m_periodSize);
                break;
            default:
                break;
            }
        }
    }

    std::vector<std::pair<std::string, std::string>> enumerateMIDIDevices() const
    {
        std::vector<std::pair<std::string, std::string>> ret;
        int status;
        int card = -1;  /* use -1 to prime the pump of iterating through card list */

        if ((status = snd_card_next(&card)) < 0)
            return {};
        if (card < 0)
            return {};

        while (card >= 0)
        {
            snd_ctl_t *ctl;
            char name[32];
            int device = -1;
            int status;
            sprintf(name, "hw:%d", card);
            if ((status = snd_ctl_open(&ctl, name, 0)) < 0)
                continue;

            do {
                status = snd_ctl_rawmidi_next_device(ctl, &device);
                if (status < 0)
                    break;
                if (device >= 0)
                {
                    snd_rawmidi_info_t *info;
                    snd_rawmidi_info_alloca(&info);
                    snd_rawmidi_info_set_device(info, device);
                    sprintf(name + strlen(name), ",%d", device);
                    ret.push_back(std::make_pair(name, snd_rawmidi_info_get_name(info)));
                }
            } while (device >= 0);

            snd_ctl_close(ctl);

            if ((status = snd_card_next(&card)) < 0)
                break;
        }

        return ret;
    }

    static void MIDIReceiveProc(snd_rawmidi_t* midi, const ReceiveFunctor& receiver, bool& running)
    {
        uint8_t buf[512];
        while (running)
        {
            int rdBytes = snd_rawmidi_read(midi, buf, 512);
            if (rdBytes < 0)
            {
                Log.report(logvisor::Error, "MIDI connection lost");
                running = false;
                break;
            }

            receiver(std::vector<uint8_t>(std::cbegin(buf), std::cbegin(buf) + rdBytes));
        }
    }

    struct MIDIIn : public IMIDIIn
    {
        bool m_midiRunning = true;
        snd_rawmidi_t* m_midi;
        std::thread m_midiThread;

        MIDIIn(snd_rawmidi_t* midi, bool virt, ReceiveFunctor&& receiver)
        : IMIDIIn(virt, std::move(receiver)), m_midi(midi),
          m_midiThread(std::bind(MIDIReceiveProc, m_midi, m_receiver, m_midiRunning)) {}

        ~MIDIIn()
        {
            m_midiRunning = false;
            pthread_kill(m_midiThread.native_handle(), SIGTERM);
            if (m_midiThread.joinable())
                m_midiThread.join();
            snd_rawmidi_close(m_midi);
        }

        std::string description() const
        {
            snd_rawmidi_info_t* info;
            snd_rawmidi_info_alloca(&info);
            snd_rawmidi_info(m_midi, info);
            std::string ret = snd_rawmidi_info_get_name(info);
            return ret;
        }
    };

    struct MIDIOut : public IMIDIOut
    {
        snd_rawmidi_t* m_midi;
        MIDIOut(snd_rawmidi_t* midi, bool virt)
        : IMIDIOut(virt), m_midi(midi) {}

        ~MIDIOut() {snd_rawmidi_close(m_midi);}

        std::string description() const
        {
            snd_rawmidi_info_t* info;
            snd_rawmidi_info_alloca(&info);
            snd_rawmidi_info(m_midi, info);
            std::string ret = snd_rawmidi_info_get_name(info);
            return ret;
        }

        size_t send(const void* buf, size_t len) const
        {
            return size_t(std::max(0l, snd_rawmidi_write(m_midi, buf, len)));
        }
    };

    struct MIDIInOut : public IMIDIInOut
    {
        bool m_midiRunning = true;
        snd_rawmidi_t* m_midiIn;
        snd_rawmidi_t* m_midiOut;
        std::thread m_midiThread;

        MIDIInOut(snd_rawmidi_t* midiIn, snd_rawmidi_t* midiOut, bool virt, ReceiveFunctor&& receiver)
        : IMIDIInOut(virt, std::move(receiver)), m_midiIn(midiIn), m_midiOut(midiOut),
          m_midiThread(std::bind(MIDIReceiveProc, m_midiIn, m_receiver, m_midiRunning)) {}

        ~MIDIInOut()
        {
            m_midiRunning = false;
            pthread_kill(m_midiThread.native_handle(), SIGTERM);
            if (m_midiThread.joinable())
                m_midiThread.join();
            snd_rawmidi_close(m_midiIn);
            snd_rawmidi_close(m_midiOut);
        }

        std::string description() const
        {
            snd_rawmidi_info_t* info;
            snd_rawmidi_info_alloca(&info);
            snd_rawmidi_info(m_midiIn, info);
            std::string ret = snd_rawmidi_info_get_name(info);
            return ret;
        }

        size_t send(const void* buf, size_t len) const
        {
            return size_t(std::max(0l, snd_rawmidi_write(m_midiOut, buf, len)));
        }
    };

    std::unique_ptr<IMIDIIn> newVirtualMIDIIn(ReceiveFunctor&& receiver)
    {
        int status;
        snd_rawmidi_t* midi;
        status = snd_rawmidi_open(&midi, nullptr, "virtual", 0);
        if (status)
            return {};
        return std::make_unique<MIDIIn>(midi, true, std::move(receiver));
    }

    std::unique_ptr<IMIDIOut> newVirtualMIDIOut()
    {
        int status;
        snd_rawmidi_t* midi;
        status = snd_rawmidi_open(nullptr, &midi, "virtual", 0);
        if (status)
            return {};
        return std::make_unique<MIDIOut>(midi, true);
    }

    std::unique_ptr<IMIDIInOut> newVirtualMIDIInOut(ReceiveFunctor&& receiver)
    {
        int status;
        snd_rawmidi_t* midiIn;
        snd_rawmidi_t* midiOut;
        status = snd_rawmidi_open(&midiIn, &midiOut, "virtual", 0);
        if (status)
            return {};
        return std::make_unique<MIDIInOut>(midiIn, midiOut, true, std::move(receiver));
    }

    std::unique_ptr<IMIDIIn> newRealMIDIIn(const char* name, ReceiveFunctor&& receiver)
    {
        snd_rawmidi_t* midi;
        int status = snd_rawmidi_open(&midi, nullptr, name, 0);
        if (status)
            return {};
        return std::make_unique<MIDIIn>(midi, true, std::move(receiver));
    }

    std::unique_ptr<IMIDIOut> newRealMIDIOut(const char* name)
    {
        snd_rawmidi_t* midi;
        int status = snd_rawmidi_open(nullptr, &midi, name, 0);
        if (status)
            return {};
        return std::make_unique<MIDIOut>(midi, true);
    }

    std::unique_ptr<IMIDIInOut> newRealMIDIInOut(const char* name, ReceiveFunctor&& receiver)
    {
        snd_rawmidi_t* midiIn;
        snd_rawmidi_t* midiOut;
        int status = snd_rawmidi_open(&midiIn, &midiOut, name, 0);
        if (status)
            return {};
        return std::make_unique<MIDIInOut>(midiIn, midiOut, true, std::move(receiver));
    }
};

std::unique_ptr<IAudioVoiceEngine> NewAudioVoiceEngine()
{
    std::unique_ptr<IAudioVoiceEngine> ret = std::make_unique<ALSAAudioVoiceEngine>();
    if (!static_cast<ALSAAudioVoiceEngine&>(*ret).m_pcm)
        return {};
    return ret;
}

}
