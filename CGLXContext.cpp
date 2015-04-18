#include "CGLXContext.hpp"
#include <iostream>

CGLXContext::CGLXContext()
{
    std::cout << "Hello from GLX" << std::endl;
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
