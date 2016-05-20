#ifndef BOO_IMIDIPORT_HPP
#define BOO_IMIDIPORT_HPP

#include <string>
#include <functional>

namespace boo
{

class IMIDIPort
{
public:
    virtual ~IMIDIPort() = default;
    virtual bool isVirtual() const=0;
    virtual std::string description() const=0;
};

class IMIDIIn : public IMIDIPort
{
public:
    virtual size_t receive(void* buf, size_t len) const=0;
};

class IMIDIOut : public IMIDIPort
{
public:
    virtual size_t send(const void* buf, size_t len) const=0;
};

class IMIDIInOut : public IMIDIPort
{
public:
    virtual size_t send(const void* buf, size_t len) const=0;
    virtual size_t receive(void* buf, size_t len) const=0;
};

}

#endif // BOO_IMIDIPORT_HPP
