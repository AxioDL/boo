#include <memory>
#include <list>
#include "AudioVoiceEngine.hpp"
#include "logvisor/logvisor.hpp"

#include <alsa/asoundlib.h>

namespace boo
{
static logvisor::Module Log("boo::ALSA");

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
        }
        else if (!snd_pcm_hw_params_test_rate(m_pcm, hwParams, 48000, 0))
        {
            bestRate = 48000;
            m_mixInfo.m_sampleRate = 48000.0;
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
                    for (int c=0 ; c<chm->channels ; ++c)
                        chBits |= 1 << chm->pos[c];

                    bool good = false;
                    switch (m_mixInfo.m_channels)
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
            chmapOut.m_channelCount = chCount;
            for (int c=0 ; c<foundChmap->channels ; ++c)
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
};

std::unique_ptr<IAudioVoiceEngine> NewAudioVoiceEngine()
{
    std::unique_ptr<IAudioVoiceEngine> ret = std::make_unique<ALSAAudioVoiceEngine>();
    if (!static_cast<ALSAAudioVoiceEngine&>(*ret).m_pcm)
        return {};
    return ret;
}

}
