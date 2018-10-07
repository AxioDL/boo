#pragma once

#include <string>
#include <functional>
#include <vector>
#include <cstdint>

namespace boo
{
struct IAudioVoiceEngine;
using ReceiveFunctor = std::function<void(std::vector<uint8_t>&&, double time)>;

class IMIDIPort
{
    bool m_virtual;
protected:
    IAudioVoiceEngine* m_parent;
    IMIDIPort(IAudioVoiceEngine* parent, bool virt) : m_virtual(virt), m_parent(parent) {}
public:
    virtual ~IMIDIPort();
    bool isVirtual() const {return m_virtual;}
    virtual std::string description() const=0;
    void _disown() { m_parent = nullptr; }
};

class IMIDIReceiver
{
public:
    ReceiveFunctor m_receiver;
    IMIDIReceiver(ReceiveFunctor&& receiver) : m_receiver(std::move(receiver)) {}
};

class IMIDIIn : public IMIDIPort, public IMIDIReceiver
{
protected:
    IMIDIIn(IAudioVoiceEngine* parent, bool virt, ReceiveFunctor&& receiver)
    : IMIDIPort(parent, virt), IMIDIReceiver(std::move(receiver)) {}
public:
    virtual ~IMIDIIn();
};

class IMIDIOut : public IMIDIPort
{
protected:
    IMIDIOut(IAudioVoiceEngine* parent, bool virt) : IMIDIPort(parent, virt) {}
public:
    virtual ~IMIDIOut();
    virtual size_t send(const void* buf, size_t len) const=0;
};

class IMIDIInOut : public IMIDIPort, public IMIDIReceiver
{
protected:
    IMIDIInOut(IAudioVoiceEngine* parent, bool virt, ReceiveFunctor&& receiver)
    : IMIDIPort(parent, virt), IMIDIReceiver(std::move(receiver)) {}
public:
    virtual ~IMIDIInOut();
    virtual size_t send(const void* buf, size_t len) const=0;
};

}

