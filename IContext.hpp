#ifndef ICONTEXT_HPP
#define ICONTEXT_HPP

#include <string>

class IContext
{
public:
    virtual ~IContext() {}

    virtual const std::string version() const=0;
    virtual const std::string name() const=0;
    virtual int depthSize() const=0;
    virtual int redDepth() const=0;
    virtual int greenDepth() const=0;
    virtual int blueDepth() const=0;
};

#endif // ICONTEXT_HPP
