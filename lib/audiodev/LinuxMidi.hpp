#ifndef BOO_LINUXMIDI_HPP
#define BOO_LINUXMIDI_HPP

#include "AudioVoiceEngine.hpp"
#include "logvisor/logvisor.hpp"
#include <thread>

#include <alsa/asoundlib.h>
#include <signal.h>

namespace boo
{
extern logvisor::Module ALSALog;

static inline double TimespecToDouble(struct timespec& ts)
{
    return ts.tv_sec + ts.tv_nsec / 1.0e9;
}

struct LinuxMidi : BaseAudioVoiceEngine
{
    std::vector<std::pair<std::string, std::string>> enumerateMIDIInputs() const
    {
        std::vector<std::pair<std::string, std::string>> ret;
        int status;
        int card = -1;  /* use -1 to prime the pump of iterating through card list */

        if ((status = snd_card_next(&card)) < 0)
            return {};
        if (card < 0)
            return {};

        snd_rawmidi_info_t* info;
        snd_rawmidi_info_malloc(&info);

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
                    snd_rawmidi_info_set_device(info, device);
                    if (snd_rawmidi_info_get_stream(info) != SND_RAWMIDI_STREAM_INPUT)
                        continue;
                    sprintf(name + strlen(name), ",%d", device);
                    ret.push_back(std::make_pair(name, snd_rawmidi_info_get_name(info)));
                }
            } while (device >= 0);

            snd_ctl_close(ctl);

            if ((status = snd_card_next(&card)) < 0)
                break;
        }

        snd_rawmidi_info_free(info);

        return ret;
    }

    static void MIDIFreeProc(void* midiStatus)
    {
        snd_rawmidi_status_free((snd_rawmidi_status_t*)midiStatus);
    }

    static void MIDIReceiveProc(snd_rawmidi_t* midi, const ReceiveFunctor& receiver)
    {
        logvisor::RegisterThreadName("Boo MIDI");
        snd_rawmidi_status_t* midiStatus;
        snd_rawmidi_status_malloc(&midiStatus);
        pthread_cleanup_push(MIDIFreeProc, midiStatus);

        uint8_t buf[512];
        while (true)
        {
            snd_htimestamp_t ts;
            snd_rawmidi_status(midi, midiStatus);
            snd_rawmidi_status_get_tstamp(midiStatus, &ts);
            int rdBytes = snd_rawmidi_read(midi, buf, 512);
            if (rdBytes < 0)
            {
                if (rdBytes != -EINTR)
                {
                    ALSALog.report(logvisor::Error, "MIDI connection lost");
                    break;
                }
                continue;
            }

            int oldtype;
            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldtype);
            receiver(std::vector<uint8_t>(std::cbegin(buf), std::cbegin(buf) + rdBytes), TimespecToDouble(ts));
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldtype);
            pthread_testcancel();
        }

        pthread_cleanup_pop(1);
    }

    struct MIDIIn : public IMIDIIn
    {
        snd_rawmidi_t* m_midi;
        std::thread m_midiThread;

        MIDIIn(snd_rawmidi_t* midi, bool virt, ReceiveFunctor&& receiver)
            : IMIDIIn(virt, std::move(receiver)), m_midi(midi),
              m_midiThread(std::bind(MIDIReceiveProc, m_midi, m_receiver)) {}

        ~MIDIIn()
        {
            pthread_cancel(m_midiThread.native_handle());
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
        snd_rawmidi_t* m_midiIn;
        snd_rawmidi_t* m_midiOut;
        std::thread m_midiThread;

        MIDIInOut(snd_rawmidi_t* midiIn, snd_rawmidi_t* midiOut, bool virt, ReceiveFunctor&& receiver)
            : IMIDIInOut(virt, std::move(receiver)), m_midiIn(midiIn), m_midiOut(midiOut),
              m_midiThread(std::bind(MIDIReceiveProc, m_midiIn, m_receiver)) {}

        ~MIDIInOut()
        {
            pthread_cancel(m_midiThread.native_handle());
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

    bool useMIDILock() const {return true;}
};

}

#endif // BOO_LINUXMIDI_HPP
