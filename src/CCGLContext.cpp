#include "CCGLContext.hpp"
#include <iostream>

CCGLContext::CCGLContext()
{
    std::cout << "Hello from CGL" << std::endl;
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
