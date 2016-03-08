#include <memory>
#include <list>
#include "boo/audiodev/IAudioVoiceAllocator.hpp"
#include "logvisor/logvisor.hpp"

#include <alsa/asoundlib.h>
#include <signal.h>

namespace boo
{
static logvisor::Module Log("boo::ALSA");
struct ALSAAudioVoiceAllocator;

struct ALSAAudioVoice : IAudioVoice
{
    ALSAAudioVoiceAllocator& m_parent;
    std::list<ALSAAudioVoice*>::iterator m_parentIt;

    ChannelMap m_map;
    IAudioVoiceCallback* m_cb;
    snd_pcm_t* m_pcm = nullptr;
    snd_pcm_uframes_t m_bufSize;
    snd_pcm_uframes_t m_periodSize;

    const ChannelMap& channelMap() const {return m_map;}

    ALSAAudioVoice(ALSAAudioVoiceAllocator& parent, AudioChannelSet set,
                   unsigned sampleRate, IAudioVoiceCallback* cb)
    : m_parent(parent), m_cb(cb)
    {
        if (snd_pcm_open(&m_pcm, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_ASYNC) < 0)
        {
            Log.report(logvisor::Error, "unable to allocate ALSA voice");
            return;
        }

        unsigned chCount = ChannelCount(set);
        int err;
        while ((err = snd_pcm_set_params(m_pcm, SND_PCM_FORMAT_S16, SND_PCM_ACCESS_RW_INTERLEAVED,
                                         chCount, sampleRate, 1, 100000)) < 0)
        {
            if (set == AudioChannelSet::Stereo)
                break;
            set = AudioChannelSet(int(set) - 1);
            chCount = ChannelCount(set);
        }
        if (err < 0)
        {
            snd_pcm_close(m_pcm);
            m_pcm = nullptr;
            Log.report(logvisor::Error, "unable to set ALSA voice params");
            return;
        }

        snd_pcm_chmap_query_t** chmaps = snd_pcm_query_chmaps(m_pcm);
        if (chmaps)
        {
            snd_pcm_chmap_t* foundChmap = nullptr;
            for (snd_pcm_chmap_query_t** chmap = chmaps ; *chmap != nullptr ; ++chmap)
            {
                if ((*chmap)->map.channels == chCount)
                {
                    snd_pcm_chmap_t* chm = &(*chmap)->map;
                    uint64_t chBits = 0;
                    for (int c=0 ; c<chm->channels ; ++c)
                        chBits |= 1 << chm->pos[c];

                    bool good = false;
                    switch (set)
                    {
                    case AudioChannelSet::Stereo:
                        if ((chBits & (1 << SND_CHMAP_FL)) != 0 &&
                            (chBits & (1 << SND_CHMAP_FR)) != 0)
                            good = true;
                        break;
                    case AudioChannelSet::Quad:
                        if ((chBits & (1 << SND_CHMAP_FL)) != 0 &&
                            (chBits & (1 << SND_CHMAP_FR)) != 0 &&
                            (chBits & (1 << SND_CHMAP_RL)) != 0 &&
                            (chBits & (1 << SND_CHMAP_RR)) != 0)
                            good = true;
                        break;
                    case AudioChannelSet::Surround51:
                        if ((chBits & (1 << SND_CHMAP_FL)) != 0 &&
                            (chBits & (1 << SND_CHMAP_FR)) != 0 &&
                            (chBits & (1 << SND_CHMAP_RL)) != 0 &&
                            (chBits & (1 << SND_CHMAP_RR)) != 0 &&
                            (chBits & (1 << SND_CHMAP_FC)) != 0 &&
                            (chBits & (1 << SND_CHMAP_LFE)) != 0)
                            good = true;
                        break;
                    case AudioChannelSet::Surround71:
                        if ((chBits & (1 << SND_CHMAP_FL)) != 0 &&
                            (chBits & (1 << SND_CHMAP_FR)) != 0 &&
                            (chBits & (1 << SND_CHMAP_RL)) != 0 &&
                            (chBits & (1 << SND_CHMAP_RR)) != 0 &&
                            (chBits & (1 << SND_CHMAP_FC)) != 0 &&
                            (chBits & (1 << SND_CHMAP_LFE)) != 0 &&
                            (chBits & (1 << SND_CHMAP_SL)) != 0 &&
                            (chBits & (1 << SND_CHMAP_SR)) != 0)
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
            m_map.m_channelCount = chCount;
            for (int c=0 ; c<foundChmap->channels ; ++c)
                m_map.m_channels[c] = AudioChannel(foundChmap->pos[c] - 3);
            snd_pcm_set_chmap(m_pcm, foundChmap);
            snd_pcm_free_chmaps(chmaps);
        }
        else
        {
            m_map.m_channelCount = 2;
            m_map.m_channels[0] = AudioChannel::FrontLeft;
            m_map.m_channels[1] = AudioChannel::FrontRight;
        }

        snd_pcm_get_params(m_pcm, &m_bufSize, &m_periodSize);
        snd_pcm_prepare(m_pcm);

        pump();
    }

    ~ALSAAudioVoice();

    void bufferSampleData(const int16_t* data, size_t frames)
    {
        if (m_pcm)
            snd_pcm_writei(m_pcm, data, frames);
    }

    void start()
    {
        if (m_pcm)
            snd_pcm_start(m_pcm);
    }

    void stop()
    {
        if (m_pcm)
            snd_pcm_drain(m_pcm);
    }

    void pump()
    {
        snd_pcm_sframes_t frames = snd_pcm_avail(m_pcm);
        if (frames < 0)
        {
            snd_pcm_state_t st = snd_pcm_state(m_pcm);
            if (st == SND_PCM_STATE_XRUN)
            {
                snd_pcm_prepare(m_pcm);
                frames = snd_pcm_avail(m_pcm);
                fprintf(stderr, "REC %ld\n", frames);
            }
            else
                return;
        }
        if (frames < 0)
            return;
        snd_pcm_sframes_t buffers = frames / m_periodSize;
        for (snd_pcm_sframes_t b=0 ; b<buffers ; ++b)
            m_cb->needsNextBuffer(*this, m_periodSize);
    }
};

struct ALSAAudioVoiceAllocator : IAudioVoiceAllocator
{
    std::list<ALSAAudioVoice*> m_allocatedVoices;

    std::unique_ptr<IAudioVoice> allocateNewVoice(AudioChannelSet layoutOut,
                                                  unsigned sampleRate,
                                                  IAudioVoiceCallback* cb)
    {
        ALSAAudioVoice* newVoice = new ALSAAudioVoice(*this, layoutOut, sampleRate, cb);
        newVoice->m_parentIt = m_allocatedVoices.insert(m_allocatedVoices.end(), newVoice);
        std::unique_ptr<IAudioVoice> ret(newVoice);
        if (!newVoice->m_pcm)
            return {};
        return ret;
    }

    AudioChannelSet getAvailableSet()
    {
        snd_pcm_t* pcm;
        if (snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_ASYNC) < 0)
        {
            Log.report(logvisor::Error, "unable to allocate ALSA voice");
            return AudioChannelSet::Unknown;
        }

        snd_pcm_chmap_query_t** chmaps = snd_pcm_query_chmaps(pcm);
        if (!chmaps)
        {
            snd_pcm_close(pcm);
            return AudioChannelSet::Stereo;
        }
        static const std::array<AudioChannelSet, 4> testSets =
            {AudioChannelSet::Surround71, AudioChannelSet::Surround51,
             AudioChannelSet::Quad, AudioChannelSet::Stereo};
        for (AudioChannelSet set : testSets)
        {
            for (snd_pcm_chmap_query_t** chmap = chmaps ; *chmap != nullptr ; ++chmap)
            {
                snd_pcm_chmap_t* chm = &(*chmap)->map;
                uint64_t chBits = 0;
                for (int c=0 ; c<chm->channels ; ++c)
                    chBits |= 1 << chm->pos[c];

                switch (set)
                {
                case AudioChannelSet::Stereo:
                    if ((chBits & (1 << SND_CHMAP_FL)) != 0 &&
                        (chBits & (1 << SND_CHMAP_FR)) != 0)
                    {
                        snd_pcm_free_chmaps(chmaps);
                        return AudioChannelSet::Stereo;
                    }
                    break;
                case AudioChannelSet::Quad:
                    if ((chBits & (1 << SND_CHMAP_FL)) != 0 &&
                        (chBits & (1 << SND_CHMAP_FR)) != 0 &&
                        (chBits & (1 << SND_CHMAP_RL)) != 0 &&
                        (chBits & (1 << SND_CHMAP_RR)) != 0)
                    {
                        snd_pcm_free_chmaps(chmaps);
                        return AudioChannelSet::Quad;
                    }
                    break;
                case AudioChannelSet::Surround51:
                    if ((chBits & (1 << SND_CHMAP_FL)) != 0 &&
                        (chBits & (1 << SND_CHMAP_FR)) != 0 &&
                        (chBits & (1 << SND_CHMAP_RL)) != 0 &&
                        (chBits & (1 << SND_CHMAP_RR)) != 0 &&
                        (chBits & (1 << SND_CHMAP_FC)) != 0 &&
                        (chBits & (1 << SND_CHMAP_LFE)) != 0)
                    {
                        snd_pcm_free_chmaps(chmaps);
                        return AudioChannelSet::Surround51;
                    }
                    break;
                case AudioChannelSet::Surround71:
                    if ((chBits & (1 << SND_CHMAP_FL)) != 0 &&
                        (chBits & (1 << SND_CHMAP_FR)) != 0 &&
                        (chBits & (1 << SND_CHMAP_RL)) != 0 &&
                        (chBits & (1 << SND_CHMAP_RR)) != 0 &&
                        (chBits & (1 << SND_CHMAP_FC)) != 0 &&
                        (chBits & (1 << SND_CHMAP_LFE)) != 0 &&
                        (chBits & (1 << SND_CHMAP_SL)) != 0 &&
                        (chBits & (1 << SND_CHMAP_SR)) != 0)
                    {
                        snd_pcm_free_chmaps(chmaps);
                        return AudioChannelSet::Surround71;
                    }
                    break;
                default: break;
                }
            }
        }

        snd_pcm_free_chmaps(chmaps);
        return AudioChannelSet::Unknown;
    }

    void pumpVoices()
    {
        for (ALSAAudioVoice* vox : m_allocatedVoices)
            vox->pump();
    }
};

ALSAAudioVoice::~ALSAAudioVoice()
{
    if (m_pcm)
        snd_pcm_close(m_pcm);
    m_parent.m_allocatedVoices.erase(m_parentIt);
}

std::unique_ptr<IAudioVoiceAllocator> NewAudioVoiceAllocator()
{
    return std::make_unique<ALSAAudioVoiceAllocator>();
}

}
