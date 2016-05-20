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

class IMIDIIn : public IMIDIPort
{
protected:
    ReceiveFunctor m_receiver;
    IMIDIIn(bool virt, ReceiveFunctor&& receiver)
    : IMIDIPort(virt), m_receiver(std::move(receiver)) {}
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

class IMIDIInOut : public IMIDIPort
{
protected:
    ReceiveFunctor m_receiver;
    IMIDIInOut(bool virt, ReceiveFunctor&& receiver)
    : IMIDIPort(virt), m_receiver(std::move(receiver)) {}
public:
    virtual ~IMIDIInOut();
    virtual size_t send(const void* buf, size_t len) const=0;
};

}

#endif // BOO_IMIDIPORT_HPP
