#ifndef CGLXCONTEXT_HPP
#define CGLXCONTEXT_HPP

#include <GL/glx.h>

#include <IContext.hpp>

class CGLXContext : public IContext
{
public:
    CGLXContext();
    virtual ~CGLXContext() {}

    const std::string version() const override;
    const std::string name() const override;
    int depthSize() const override;
    int redDepth() const override;
    int greenDepth() const override;
    int blueDepth() const override;
};



#endif // CGLXCONTEXT_HPP
