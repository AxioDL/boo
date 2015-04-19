#ifndef CGLXCONTEXT_HPP
#define CGLXCONTEXT_HPP

#include <GL/glx.h>

#include <IGraphicsContext.hpp>

class CGLXContext final : public IGraphicsContext
{
public:
    CGLXContext();
    virtual ~CGLXContext() {}

    bool create();
    void setMajorVersion(const int& maj) override;
    void setMinVersion(const int& min) override;
    const std::string version() const override;
    const std::string name() const override;
    int depthSize() const override;
    int redDepth() const override;
    int greenDepth() const override;
    int blueDepth() const override;
private:
    int m_majVersion;
    int m_minVersion;

    Display* m_display;
};



#endif // CGLXCONTEXT_HPP
