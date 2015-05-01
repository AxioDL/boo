#ifdef __APPLE__
#include "CCGLContext.hpp"
#include <iostream>

namespace boo
{

CCGLContext::CCGLContext()
    : m_minVersion(3),
      m_majVersion(3)
{
    std::cout << "Hello from CGL" << std::endl;
}

CCGLContext::~CCGLContext()
{
    
}

bool CCGLContext::create()
{
    return true;
}

void CCGLContext::setMinVersion(const int& min)
{
    m_minVersion = min;
}

void CCGLContext::setMajorVersion(const int& maj)
{
    m_majVersion = maj;
}

const std::string CCGLContext::version() const
{
    return "Invalid version";
}

const std::string CCGLContext::name() const
{
    return "GLX Context";
}

int CCGLContext::depthSize() const
{
    return -1;
}

int CCGLContext::redDepth() const
{
    return -1;
}

int CCGLContext::greenDepth() const
{
    return -1;
}

int CCGLContext::blueDepth() const
{
    return -1;
}
    
}

#endif