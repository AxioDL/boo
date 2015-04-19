#ifndef CCGLCONTEXT_HPP
#define CCGLCONTEXT_HPP

#ifdef __APPLE__
#include "IGraphicsContext.hpp"
#include <OpenGL/OpenGL.h>

class CCGLContext final : public IGraphicsContext
{
public:
    CCGLContext();
    virtual ~CCGLContext();
    
    bool create();
    void setMinVersion  (const int& min) override;
    void setMajorVersion(const int& maj) override;
    const std::string version() const override;
    const std::string name() const override;
    int depthSize() const override;
    int redDepth() const override;
    int greenDepth() const override;
    int blueDepth() const override;
private:
    int m_minVersion;
    int m_majVersion;
};

#endif // __APPLE__
#endif // CCGLCONTEXT_HPP
