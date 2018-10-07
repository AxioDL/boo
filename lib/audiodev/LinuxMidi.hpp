#pragma once

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
    std::unordered_map<std::string, IMIDIPort*> m_openHandles;
    void _addOpenHandle(const char* name, IMIDIPort* port)
    {
        m_openHandles[name] = port;
    }
    void _removeOpenHandle(IMIDIPort* port)
    {
        for (auto it = m_openHandles.begin(); it != m_openHandles.end();)
        {
            if (it->second == port)
            {
                it = m_openHandles.erase(it);
                continue;
            }
            ++it;
        }
    }

    ~LinuxMidi()
    {
        for (auto& p : m_openHandles)
            p.second->_disown();
    }

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
                    sprintf(name + strlen(name), ",%d", device);
                    auto search = m_openHandles.find(name);
                    if (search != m_openHandles.cend())
                    {
                        ret.push_back(std::make_pair(name, search->second->description()));
                        continue;
                    }

                    snd_rawmidi_t* midi;
                    if (!snd_rawmidi_open(&midi, nullptr, name, SND_RAWMIDI_NONBLOCK))
                    {
                        snd_rawmidi_info(midi, info);
                        ret.push_back(std::make_pair(name, snd_rawmidi_info_get_name(info)));
                        snd_rawmidi_close(midi);
                    }
                }
            } while (device >= 0);

            snd_ctl_close(ctl);

            if ((status = snd_card_next(&card)) < 0)
                break;
        }

        snd_rawmidi_info_free(info);

        return ret;
    }

    bool supportsVirtualMIDIIn() const
    {
        return true;
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

        MIDIIn(LinuxMidi* parent, snd_rawmidi_t* midi, bool virt, ReceiveFunctor&& receiver)
            : IMIDIIn(parent, virt, std::move(receiver)), m_midi(midi),
              m_midiThread(std::bind(MIDIReceiveProc, m_midi, m_receiver)) {}

        ~MIDIIn()
        {
            if (m_parent)
                static_cast<LinuxMidi*>(m_parent)->_removeOpenHandle(this);
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
        MIDIOut(LinuxMidi* parent, snd_rawmidi_t* midi, bool virt)
            : IMIDIOut(parent, virt), m_midi(midi) {}

        ~MIDIOut()
        {
            if (m_parent)
                static_cast<LinuxMidi*>(m_parent)->_removeOpenHandle(this);
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

        MIDIInOut(LinuxMidi* parent, snd_rawmidi_t* midiIn, snd_rawmidi_t* midiOut, bool virt, ReceiveFunctor&& receiver)
            : IMIDIInOut(parent, virt, std::move(receiver)), m_midiIn(midiIn), m_midiOut(midiOut),
              m_midiThread(std::bind(MIDIReceiveProc, m_midiIn, m_receiver)) {}

        ~MIDIInOut()
        {
            if (m_parent)
                static_cast<LinuxMidi*>(m_parent)->_removeOpenHandle(this);
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
        return std::make_unique<MIDIIn>(nullptr, midi, true, std::move(receiver));
    }

    std::unique_ptr<IMIDIOut> newVirtualMIDIOut()
    {
        int status;
        snd_rawmidi_t* midi;
        status = snd_rawmidi_open(nullptr, &midi, "virtual", 0);
        if (status)
            return {};
        return std::make_unique<MIDIOut>(nullptr, midi, true);
    }

    std::unique_ptr<IMIDIInOut> newVirtualMIDIInOut(ReceiveFunctor&& receiver)
    {
        int status;
        snd_rawmidi_t* midiIn;
        snd_rawmidi_t* midiOut;
        status = snd_rawmidi_open(&midiIn, &midiOut, "virtual", 0);
        if (status)
            return {};
        return std::make_unique<MIDIInOut>(nullptr, midiIn, midiOut, true, std::move(receiver));
    }

    std::unique_ptr<IMIDIIn> newRealMIDIIn(const char* name, ReceiveFunctor&& receiver)
    {
        snd_rawmidi_t* midi;
        int status = snd_rawmidi_open(&midi, nullptr, name, 0);
        if (status)
            return {};
        auto ret = std::make_unique<MIDIIn>(this, midi, true, std::move(receiver));
        _addOpenHandle(name, ret.get());
        return ret;
    }

    std::unique_ptr<IMIDIOut> newRealMIDIOut(const char* name)
    {
        snd_rawmidi_t* midi;
        int status = snd_rawmidi_open(nullptr, &midi, name, 0);
        if (status)
            return {};
        auto ret = std::make_unique<MIDIOut>(this, midi, true);
        _addOpenHandle(name, ret.get());
        return ret;
    }

    std::unique_ptr<IMIDIInOut> newRealMIDIInOut(const char* name, ReceiveFunctor&& receiver)
    {
        snd_rawmidi_t* midiIn;
        snd_rawmidi_t* midiOut;
        int status = snd_rawmidi_open(&midiIn, &midiOut, name, 0);
        if (status)
            return {};
        auto ret = std::make_unique<MIDIInOut>(this, midiIn, midiOut, true, std::move(receiver));
        _addOpenHandle(name, ret.get());
        return ret;
    }

    bool useMIDILock() const {return true;}
};

}

