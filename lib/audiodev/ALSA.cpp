#include "boo/audiodev/IAudioVoiceAllocator.hpp"
#include <alsa/asoundlib.h>
#include <LogVisor/LogVisor.hpp>

namespace boo
{
static LogVisor::LogModule Log("boo::ALSA");

struct ALSAAudioVoice : IAudioVoice
{
    ChannelMap m_map;
    IAudioVoiceCallback* m_cb;
    snd_pcm_t* m_pcm = nullptr;
    snd_async_handler_t* m_handler = nullptr;
    snd_pcm_uframes_t m_bufSize;
    snd_pcm_uframes_t m_periodSize;

    const ChannelMap& channelMap() const {return m_map;}

    static void Callback(snd_async_handler_t* handler)
    {
        ALSAAudioVoice* voice = static_cast<ALSAAudioVoice*>(snd_async_handler_get_callback_private(handler));
        voice->m_cb->needsNextBuffer(voice, voice->m_periodSize);
    }

    ALSAAudioVoice(AudioChannelSet set, unsigned sampleRate, IAudioVoiceCallback* cb)
    : m_cb(cb)
    {
        if (snd_pcm_open(&m_pcm, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_ASYNC) < 0)
        {
            Log.report(LogVisor::Error, "unable to allocate ALSA voice");
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
            Log.report(LogVisor::Error, "unable to set ALSA voice params");
            return;
        }

        snd_pcm_chmap_query_t** chmaps = snd_pcm_query_chmaps(m_pcm);
        if (!chmaps)
        {
            snd_pcm_close(m_pcm);
            m_pcm = nullptr;
            Log.report(LogVisor::Error, "unable to query ALSA voice chmaps");
            return;
        }
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
            Log.report(LogVisor::Error, "unable to find matching ALSA voice chmap");
            return;
        }
        m_map.m_channelCount = chCount;
        for (int c=0 ; c<foundChmap->channels ; ++c)
            m_map.m_channels[c] = AudioChannel(foundChmap->pos[c] - 3);
        snd_pcm_set_chmap(m_pcm, foundChmap);
        snd_pcm_free_chmaps(chmaps);

        snd_async_add_pcm_handler(&m_handler, m_pcm, snd_async_callback_t(Callback), this);
        snd_pcm_get_params(m_pcm, &m_bufSize, &m_periodSize);
    }

    ~ALSAAudioVoice()
    {
        if (m_handler)
            snd_async_del_handler(m_handler);
        if (m_pcm)
            snd_pcm_close(m_pcm);
    }

    void bufferSampleData(const int16_t* data, size_t frames)
    {
        if (m_pcm)
            snd_pcm_writei(m_pcm, data, frames);
    }
};

struct ALSAAudioVoiceAllocator : IAudioVoiceAllocator
{
    std::unique_ptr<IAudioVoice> allocateNewVoice(AudioChannelSet layoutOut,
                                                  unsigned sampleRate,
                                                  IAudioVoiceCallback* cb)
    {
        ALSAAudioVoice* newVoice = new ALSAAudioVoice(layoutOut, sampleRate, cb);
        std::unique_ptr<IAudioVoice> ret(newVoice);
        if (!newVoice->m_pcm)
            return {};
        return ret;
    }
};

}
