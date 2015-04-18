#ifndef BOO_HPP
#define BOO_HPP

#include "IContext.hpp"

#if defined(_WIN32)
#error "No support for WGL"
#elif defined(__APPLE__)
#include "CCGLContext.hpp"
typedef CCGLContext CContext;
#elif __linux__
#include "CGLXContext.hpp"
typedef CGLXContext CContext;
#endif

#endif // BOO_HPP
