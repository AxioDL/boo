#if !defined(__APPLE__) && (defined(__linux__) || defined(BSD))
#include "x11/CGLXContext.hpp"
#include <iostream>

namespace boo
{

CGLXContext::CGLXContext()
    : m_majVersion(3),
      m_minVersion(3),
      m_display(nullptr)
{
    std::cout << "Hello from GLX" << std::endl;
}

bool CGLXContext::create()
{
    return true;
}

void CGLXContext::setMinVersion(const int& min)
{
    m_minVersion = min;
}

void CGLXContext::setMajorVersion(const int& maj)
{
    m_majVersion = maj;
}

const std::string CGLXContext::version() const
{
    return "Invalid version";
}

const std::string CGLXContext::name() const
{
    return "GLX Context";
}

int CGLXContext::depthSize() const
{
    return -1;
}

int CGLXContext::redDepth() const
{
    return -1;
}

int CGLXContext::greenDepth() const
{
    return -1;
}

int CGLXContext::blueDepth() const
{
    return -1;
}

}

#endif // !defined(__APPLE__) && (defined(__linux__) || defined(BSD))
