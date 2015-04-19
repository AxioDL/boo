#ifndef BOO_HPP
#define BOO_HPP

#include "IGraphicsContext.hpp"

#if defined(_WIN32)
#error "No support for WGL"
#elif defined(__APPLE__)
#include "CCGLContext.hpp"
typedef CCGLContext CGraphicsContext;
#elif __linux__
#include "CGLXContext.hpp"
typedef CGLXContext CGraphicsContext;
#endif

#endif // BOO_HPP
