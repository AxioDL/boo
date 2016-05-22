#ifndef BOO_IMIDIPORT_HPP
#define BOO_IMIDIPORT_HPP

#include <string>
#include <functional>
#include <vector>
#include <stdint.h>

namespace boo
{

using ReceiveFunctor = std::function<void(std::vector<uint8_t>&&)>;

class IMIDIPort
{
    bool m_virtual;
protected:
    IMIDIPort(bool virt) : m_virtual(virt) {}
public:
    virtual ~IMIDIPort();
    bool isVirtual() const {return m_virtual;}
    virtual std::string description() const=0;
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
    IMIDIIn(bool virt, ReceiveFunctor&& receiver)
    : IMIDIPort(virt), IMIDIReceiver(std::move(receiver)) {}
public:
    virtual ~IMIDIIn();
};

class IMIDIOut : public IMIDIPort
{
protected:
    IMIDIOut(bool virt) : IMIDIPort(virt) {}
public:
    virtual ~IMIDIOut();
    virtual size_t send(const void* buf, size_t len) const=0;
};

class IMIDIInOut : public IMIDIPort, public IMIDIReceiver
{
protected:
    IMIDIInOut(bool virt, ReceiveFunctor&& receiver)
    : IMIDIPort(virt), IMIDIReceiver(std::move(receiver)) {}
public:
    virtual ~IMIDIInOut();
    virtual size_t send(const void* buf, size_t len) const=0;
};

}

#endif // BOO_IMIDIPORT_HPP
